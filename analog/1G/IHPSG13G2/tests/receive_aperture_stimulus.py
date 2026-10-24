"""Reconstruct imposed clocks through the actual finite source resistances."""
import bisect
import math


def source_profile(header, rows, scenario):
    column = {n: header.index(n) for n in ('time_s', 'REF', 'REF_INPUT', 'SAMPLE_N', 'SAMPLE_INPUT')}
    core, high = (1.08, 3.63) if scenario == 'low_core' else (1.32, 2.97) if scenario == 'high_core' else (1.2, 3.3)
    phase = -.3e-9 if scenario == 'early' else .3e-9 if scenario == 'late' else 0
    period = 8e-9 if scenario == 'wrong_ratio' else 4e-9
    edge = .1e-9 if scenario == 'fast_edge' else .4e-9
    ref, sample = [(0, 0)], [(0, 0)]
    for k in range(17):
        t = 24e-9 + k*8e-9
        ref += [(t, 0), (t+.4e-9, core), (t+4e-9, core), (t+4.4e-9, 0)]
    for k in range(34):
        t = 24e-9 + phase + k*period
        if t >= 156e-9-1e-15 or (scenario == 'missing' and t >= 80e-9-1e-15):
            break
        sample += [(t, 0), (t+edge, high), (t+1.5e-9, high), (t+1.5e-9+edge, 0)]
    def value(points, time):
        i = bisect.bisect_right(points, (time, math.inf))-1
        if i == len(points)-1:
            return points[i][1]
        a,b = points[i], points[i+1]
        return a[1] + (b[1]-a[1])*(time-a[0])/(b[0]-a[0])
    worst = dict(error_V=0)
    if not rows:
        raise ValueError('missing source observations')
    for row in rows:
        time = row[column['time_s']]
        for name, inside, resistance, points in [('REF', 'REF_INPUT', 28000/11, ref),
                                               ('SAMPLE_N', 'SAMPLE_INPUT', 7000, sample)]:
            source = row[column[name]] + 50/resistance*(row[column[name]]-row[column[inside]])
            expected = value(points, time)
            error = abs(source-expected)
            if not math.isfinite(error):
                raise ValueError('nonfinite source observation')
            if error > worst['error_V']:
                worst = dict(error_V=error, time_s=time, source=name,
                             reconstructed_V=source, expected_V=expected)
    return dict(passed=worst['error_V'] <= .0005, tolerance_V=.0005, worst=worst, states=len(rows))
