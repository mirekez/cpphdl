"""Open-loop sensor checks against declared source times, without alignment."""
import math

HEADER = ['time_s', 'VLOGIC', 'REF', 'SAMPLE_N', 'CLEAR', 'REF_INPUT', 'SAMPLE_INPUT',
          'REF_CLOCK', 'SAMPLE_CLOCK', 'REF_EDGE', 'SAMPLE_EDGE', 'REF_CLEAR',
          'CLEAR_DELAY', 'UP', 'DOWN']


def contract(header, rows, scenario):
    if header != HEADER or len(rows) < 100:
        raise ValueError('missing sensor trajectory')
    if any(len(r) != len(HEADER) or not all(map(math.isfinite, r)) for r in rows):
        raise ValueError('malformed sensor trajectory')
    if rows[0][0] > 2.01e-12 or rows[-1][0] < 160e-9 - 1e-15:
        raise ValueError('incomplete sensor interval')
    if any(not 0 <= b[0] - a[0] <= 2.01e-12 for a, b in zip(rows, rows[1:])):
        raise ValueError('missing fine sensor states')
    columns = {n: i for i, n in enumerate(header)}
    def crossings(name, rising=True):
        j = columns[name]
        result = []
        for a, b in zip(rows, rows[1:]):
            av, bv = a[j] - .5*a[1], b[j] - .5*b[1]
            if a[0] < 20e-9:
                continue
            if (av < 0 <= bv) if rising else (av >= 0 > bv):
                result.append(a[0] + (b[0]-a[0]) * (-av)/(bv-av))
        return result
    ref_edges, sample_edges = crossings('REF_EDGE'), crossings('SAMPLE_EDGE')
    target_ref = [24.2e-9 + k*8e-9 for k in range(17)]
    phase = -.3e-9 if scenario == 'early' else .3e-9 if scenario == 'late' else 0
    edge = .1e-9 if scenario == 'fast_edge' else .4e-9
    period = 8e-9 if scenario == 'wrong_ratio' else 4e-9
    sample_commands = [24e-9 + phase + k*period for k in range(34)
                       if 24e-9 + phase + k*period < 156e-9 - 1e-15]
    if scenario == 'missing':
        sample_commands = [t for t in sample_commands if t < 80e-9 - 1e-15]
    if scenario == 'reclear':
        target_ref = [t for t in target_ref if not 79e-9 <= t <= 85.4e-9]
        before = [t for t in sample_commands if t < 79e-9]
        after = [t for t in sample_commands if t > 85.4e-9]
        target_sample = [t+edge/2 for t in before[::2]+after[::2]]
    else:
        target_sample = [t+edge/2 for t in sample_commands[::2]]
    checks = dict(reference_edge_count=len(ref_edges) == len(target_ref),
                  divided_edge_count=len(sample_edges) == len(target_sample))
    clear_windows = [(8e-9, 15e-9)] + ([(80e-9, 84.5e-9)] if scenario == 'reclear' else [])
    checks['clear_priority'] = all(all(r[columns[n]] <= .2*r[1]
        for n in ('REF_EDGE', 'SAMPLE_EDGE', 'UP', 'DOWN'))
        for r in rows if any(a <= r[0] <= b for a, b in clear_windows))
    ref_delay = [a-b for a,b in zip(ref_edges,target_ref)]
    sample_delay = [a-b for a,b in zip(sample_edges,target_sample)]
    checks['reference_delivery_500ps'] = bool(ref_delay) and all(0 <= d <= .5e-9 for d in ref_delay)
    checks['sample_delivery_500ps'] = bool(sample_delay) and all(0 <= d <= .5e-9 for d in sample_delay)
    mismatch = []
    if scenario not in ('wrong_ratio', 'missing') and checks['reference_edge_count'] and checks['divided_edge_count']:
        mismatch = [s-r for s,r in zip(sample_delay,ref_delay)]
        checks['arm_mismatch_50ps'] = all(abs(x) <= 50e-12 for x in mismatch)
    late = [r for r in rows if 120e-9 <= r[0] <= 150e-9]
    signed_pfd = sum((r[columns['UP']]-r[columns['DOWN']])/r[1] for r in late)/len(late)
    if scenario in ('late', 'missing', 'wrong_ratio'):
        checks['phase_frequency_polarity'] = signed_pfd > .015
    elif scenario == 'early':
        checks['phase_frequency_polarity'] = signed_pfd < -.015
    if scenario == 'missing':
        checks['missing_feedback_up'] = all(r[columns['UP']] > .8*r[1] for r in late)
    exposure = max(abs(r[columns[n]]) for r in rows for n in
                   ('REF_INPUT', 'SAMPLE_INPUT', 'REF_CLOCK', 'SAMPLE_CLOCK'))
    checks['observed_core_nodes_1p65V'] = exposure <= 1.65
    return dict(checks=checks, reference_edges_s=ref_edges, sample_edges_s=sample_edges,
                reference_targets_s=target_ref, sample_targets_s=target_sample,
                reference_delays_s=ref_delay, sample_delays_s=sample_delay,
                arm_mismatch_s=mismatch, signed_pfd_average=signed_pfd,
                max_observed_core_voltage_V=exposure,
                scope='open-loop sensor/retimer; 50ps diagnostic arm budget, not receiver acquisition qualification')
