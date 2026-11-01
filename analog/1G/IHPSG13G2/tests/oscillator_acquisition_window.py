"""Electrical limits during the controller's first usable measurement window.

This is a fixed-trim cold-start test. It does not simulate the controller or
replace the separate 256..512 ns settling stress or closed-loop tests.
"""
import math

from compare import edges, interpolate

REFERENCE_PERIOD = 8e-9
CLEAR_RELEASE = 32.1e-9
START = CLEAR_RELEASE + 2 * 128 * REFERENCE_PERIOD
STOP = START + 128 * REFERENCE_PERIOD

# The physical wiring changes the transfer curve. Exercise the same lower
# trim and points up to 3/4 scale, inside the controller's 16-bit search range.
CASES = {str(code): (code, 20e-15, False) for code in (28672, 32768, 45056, 49152)}
CASES.update(loaded=(45056, 200e-15, False), refined=(45056, 20e-15, True))


def measure(rows, refined=False):
    maximum = 5e-12 if refined else 10e-12
    if len(rows) < 3 or abs(rows[-1][0] - STOP) > 1e-14 or rows[0][0] > maximum / 2:
        raise ValueError('incomplete acquisition-window transient')
    if any(not 0 < b[0] - a[0] <= maximum * 1.001 for a, b in zip(rows, rows[1:])):
        raise ValueError('unresolved acquisition-window transient')
    if any(not math.isfinite(value) for row in rows for value in row):
        raise ValueError('nonfinite acquisition-window observation')
    crossings = edges(rows, 1, .6, START, STOP)
    if len(crossings) < 3:
        raise ValueError('trim point did not sustain oscillation')
    times = [r[0] for r in rows]
    late = [r for r in rows if r[0] >= START]
    high = [interpolate(rows, times, a + (b - a) / 4, 1) for a, b in zip(crossings, crossings[1:])]
    low = [interpolate(rows, times, a + 3 * (b - a) / 4, 1) for a, b in zip(crossings, crossings[1:])]
    periods = [b - a for a, b in zip(crossings, crossings[1:])]
    result = dict(frequency_Hz=len(periods) / (crossings[-1] - crossings[0]),
        high_min_V=min(high), low_max_V=max(low), supply_min_V=min(r[4] for r in late),
        control_mean_V=sum(r[3] for r in late) / len(late),
        period_spread_s=max(periods) - min(periods), measurement_window_s=[START, STOP])
    result['checks'] = dict(rails=min(high) > 1.08 and max(low) < .12,
        supply=result['supply_min_V'] > 1.1,
        bounded=all(-.1 < r[c] < 1.3 for r in late for c in (1, 2, 3)),
        steady=result['period_spread_s'] < 80e-12)
    result['status'] = 'pass' if all(result['checks'].values()) else 'fail'
    return result
