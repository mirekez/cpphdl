"""Independent loaded TRACK sequencing and fixed-command-aperture checks."""
from bisect import bisect_left, bisect_right
import math

WARMUP, PERIOD, EDGE = 8e-6, 4e-9, .4e-9
HEADER = ['time_s', 'IN', 'VDD', 'VLOGIC', 'BIAS', 'STROBE_CORE', 'STROBE']
HEADER += [name + str(i) for i in range(13) for name in
           ('SELECT', 'TRACK', 'CLOCK_N', 'HELD', 'STATE_N', 'SET_N', 'RESET_N')]
HEADER += [name + str(i) for i in range(17) for name in ('CONTROL', 'INPUT_N', 'LOW', 'LOW_N')]
HEADER += ['EVENT', 'EVENT_DELAY', 'EVENT_DELAY_N', 'EVENT_NAND']


def contract(header, rows):
    if header != HEADER or len(rows) < 100:
        raise ValueError('missing TRACK observation interface')
    times = [r[0] for r in rows]
    if (times[0] > WARMUP - 31e-9 or times[-1] < WARMUP + 32 * PERIOD - 1e-15 or
            any(b < a for a, b in zip(times, times[1:])) or
            any(len(r) != len(HEADER) or not all(map(math.isfinite, r)) for r in rows)):
        raise ValueError('invalid or incomplete TRACK trajectory')
    if max(b - a for a, b in zip(times, times[1:])) > 2.01e-12:
        raise ValueError('missing fine TRACK states')
    column = {name: i for i, name in enumerate(HEADER)}
    def window(a, b):
        result = rows[bisect_left(times, a):bisect_right(times, b)]
        if len(result) < 2:
            raise ValueError('missing required TRACK window')
        return result
    def state_ok(row, mask):
        return all((row[column['TRACK' + str(i)]] >= .8 * row[2] if mask & (1 << i)
                    else row[column['TRACK' + str(i)]] <= .2 * row[2]) for i in range(13))
    checks = dict(startup_all_tracking=all(state_ok(r, 8191) for r in window(WARMUP - 31e-9, WARMUP - 25e-9)),
                  startup_clear=all(state_ok(r, 0) for r in window(WARMUP - 17e-9, WARMUP - 3e-9)))
    mask, events, captures = 0, [], []
    for k in range(32):
        begin = WARMUP + k * PERIOD
        selected = (k + 10) % 13
        close = (1 << selected) | ((1 << ((selected + 11) % 13)) if k == 20 else 0)
        opening = ((close << 2) | (close >> 11)) & 8191
        allow = k > 0 and k < 26 and k not in (7, 8)
        previous = mask
        mask = ((mask | (opening if allow else 0)) & ~close) if k not in (23, 24) else 0
        stable = window(begin + 3.2e-9, begin + 3.8e-9)
        events.append(dict(event=k, expected_mask=mask, passed=all(state_ok(r, mask) for r in stable)))
        # Only a previously tracking slice has a meaningful sampled target.
        if k in (23, 24):
            continue
        for i in range(13):
            if not previous & close & (1 << i):
                continue
            aperture = begin + EDGE / 2
            target_row = rows[bisect_left(times, aperture)]
            target = target_row[1]
            held_index, track_index = column['HELD' + str(i)], column['TRACK' + str(i)]
            scan = window(begin, begin + 3.8e-9)
            falls = [(a, b) for a, b in zip(scan, scan[1:])
                     if a[track_index] >= .5 * a[2] and b[track_index] < .5 * b[2]]
            errors = [r[held_index] - target for r in window(begin + 2.5e-9, begin + 3.8e-9)]
            captures.append(dict(event=k, slice=i, nominal_aperture_s=aperture,
                target_V=target, target_state_time_s=target_row[0],
                closing_halfrail_delay_s=falls[0][1][0] - aperture if len(falls) == 1 else None,
                closing_crossings=len(falls), max_fixed_target_error_V=max(map(abs, errors)),
                signed_final_error_V=errors[-1]))
    checks['event_states'] = all(e['passed'] for e in events)
    checks['one_closing_edge'] = bool(captures) and all(c['closing_crossings'] == 1 for c in captures)
    checks['fixed_aperture_20mV'] = bool(captures) and all(c['max_fixed_target_error_V'] <= .020 for c in captures)
    pulses = []
    for k in range(32):
        begin = WARMUP + k * PERIOD
        selected = window(begin, begin + 3.8e-9)
        edges = [(b[0] - begin, 'rise' if b[column['EVENT']] >= .5 * b[2] else 'fall')
                 for a, b in zip(selected, selected[1:])
                 if (a[column['EVENT']] < .5 * a[2]) != (b[column['EVENT']] < .5 * b[2])]
        idle = all(r[column['EVENT']] <= .2 * r[2] for r in window(begin + 2e-9, begin + 3.8e-9))
        pulses.append(dict(event=k, edges=edges, idle_before_next_selection=idle,
                           halfrail_width_s=edges[1][0] - edges[0][0]
                           if [x[1] for x in edges] == ['rise', 'fall'] else None))
    checks['one_event_pulse'] = all(p['halfrail_width_s'] is not None for p in pulses)
    checks['event_idle_before_next_selection'] = all(p['idle_before_next_selection'] for p in pulses)
    exposure = max(abs(r[column[name + str(i)]]) for r in rows for i in range(17)
                   for name in ('CONTROL', 'INPUT_N', 'LOW', 'LOW_N'))
    checks['low_voltage_exposure'] = exposure <= 1.65
    return dict(checks=checks, events=events, captures=captures, pulses=pulses, max_low_voltage_exposure_V=exposure,
                scope='loaded clock/local-state unit; fixed nominal command aperture; no DAC/preamp kickback or full bank qualification')
