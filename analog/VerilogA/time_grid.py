"""Keep a continuous SPICE state while bounding steps in a measurement window."""
import hashlib
import math
import re


def windowed_transient(deck, warmup_step):
    """Add an isolated, identically zero source that schedules fine breakpoints.

    The original measurement step, window, observations and device equations
    are preserved. Only the quiet warmup permits a larger maximum step.
    Callers must check timestep convergence and the observed measurement gaps.
    """
    if not math.isfinite(warmup_step) or warmup_step <= 0:
        raise ValueError('warmup step must be finite and positive')
    lines = deck.splitlines()
    analyses = [index for index, line in enumerate(lines) if line.startswith('tran ')]
    if len(analyses) != 1 or lines.count('.control') != 1:
        raise ValueError('expected one exported transient and control block')
    index = analyses[0]
    fields = lines[index].split()
    if len(fields) != 6 or fields[-1] != 'uic':
        raise ValueError('unsupported transient interface')
    try:
        output_step, stop, start, step = map(float, fields[1:5])
    except ValueError as error:
        raise ValueError('expected exported numeric timing values') from error
    if (not all(math.isfinite(value) for value in (output_step, stop, start, step))
            or not 0 < step < warmup_step < start < stop or output_step <= 0):
        raise ValueError('invalid warmup or measurement timing')
    source, node = 'Vsim_timestep_grid', 'cpphdl_sim_timestep_grid'
    if re.search(r'\b(?:' + source + '|' + node + r')\b', deck, re.I):
        raise ValueError('timing-grid source or node already exists')
    fields[4] = format(warmup_step, '.17g')
    lines[index] = ' '.join(fields)
    # Four equally spaced pulse phases, all at exactly zero volts. This
    # disconnected two-unknown MNA block carries zero current and no energy.
    pulse = ' '.join(format(value, '.17g') for value in (0, 0, start, step, step, step, 4 * step))
    card = source + ' ' + node + ' 0 PULSE(' + pulse + ')'
    lines.insert(lines.index('.control'), card)
    result = '\n'.join(lines) + '\n'
    return result, dict(start_s=start, stop_s=stop, measurement_maximum_step_s=step,
        warmup_maximum_step_s=warmup_step, source_card=card,
        original_deck_sha256=hashlib.sha256(deck.encode()).hexdigest(),
        windowed_deck_sha256=hashlib.sha256(result.encode()).hexdigest())


def check_observed_grid(rows, metadata):
    start, stop, step = (metadata[key] for key in
        ('start_s', 'stop_s', 'measurement_maximum_step_s'))
    if not all(math.isfinite(value) for value in (start, stop, step)) or not 0 < step < start < stop:
        raise ValueError('invalid measurement-grid metadata')
    times = [row[0] for row in rows]
    if (len(times) < 10 or not all(math.isfinite(time) for time in times)
            or times[0] < start - 16 * math.ulp(start) or times[0] > start + step
            or abs(times[-1] - stop) > 16 * math.ulp(stop)):
        raise ValueError('measurement grid misses its declared window')
    gaps = [b - a for a, b in zip(times, times[1:])]
    if min(gaps) < 0 or max(gaps) > step * (1 + 1e-6) + 16 * math.ulp(stop):
        raise ValueError('measurement grid exceeds its maximum step or goes backward')
    return dict(points=len(times), equal_time_observations=sum(gap == 0 for gap in gaps),
                maximum_gap_s=max(gaps), maximum_allowed_gap_s=step)
