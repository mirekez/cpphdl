"""Electrical acceptance for the process-sized reference clock experiment."""
import bisect
import statistics


def measure(rows, ppm=0, refined=False):
    stop, maximum_step = 2e-6, 10e-12 if refined else 20e-12
    if abs(rows[-1][0] - stop) > 1e-14:
        raise ValueError('clock transient did not reach the declared stop time')
    if max(b[0] - a[0] for a, b in zip(rows, rows[1:])) > maximum_step * 1.001:
        raise ValueError('clock transient has unresolved intervals')
    begin = 1.6e-6
    steady = [row for row in rows if row[0] >= begin]
    times = [row[0] for row in rows]

    def crossings(column, level, rising=True):
        result = []
        for a, b in zip(steady, steady[1:]):
            if (a[column] < level <= b[column]) if rising else (a[column] > level >= b[column]):
                result.append(a[0] + (level - a[column]) * (b[0] - a[0]) / (b[column] - a[column]))
        return result

    def value(time, column):
        index = min(max(1, bisect.bisect_left(times, time)), len(rows) - 1)
        a, b = rows[index - 1], rows[index]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    edges = {name: crossings(column, level) for name, column, level in
             (('low', 1, .6), ('out', 2, 1.65), ('divided', 4, .6), ('reference', 5, .6))}
    if any(len(points) < (80 if name in ('low', 'out') else 40) for name, points in edges.items()):
        raise ValueError('clock generator, translator or divider stopped')
    rates = {name: (len(points) - 1) / (points[-1] - points[0]) for name, points in edges.items()}
    phases = [edge - min(edges['reference'], key=lambda ref: abs(ref - edge))
              for edge in edges['divided'][1:-1]]
    delays = [edge - min(edges['low'], key=lambda low: abs(edge - low))
              for edge in edges['out'][1:-1]]
    periods = [b - a for a, b in zip(edges['out'], edges['out'][1:])]
    high, low = [], []
    for a, b in zip(edges['out'], edges['out'][1:]):
        high.append(value(a + (b - a) / 4, 2))
        low.append(value(a + 3 * (b - a) / 4, 2))
    control = [row[6] for row in steady]
    result = dict(frequency_Hz=rates, output_period_min_s=min(periods), output_period_max_s=max(periods),
                  divided_phase_mean_s=statistics.mean(phases), divided_phase_wander_s=max(phases) - min(phases),
                  delivery_min_s=min(delays), delivery_max_s=max(delays),
                  output_high_min_V=min(high), output_low_max_V=max(low),
                  logic_supply_min_V=min(row[7] for row in steady), io_supply_min_V=min(row[8] for row in steady),
                  control_min_V=min(control), control_max_V=max(control),
                  control_mean_V=statistics.mean(control), output_edges_s=edges['out'])
    expected = 250e6 * (1 + ppm * 1e-6)
    # Frequency acquisition allows 500 ppm. The settled cycle and divided
    # phase-wander limits are 1% UI (80 ps). Fixed PFD/translator delay is
    # measured separately from wander, with a one-nanosecond delivery budget.
    result['checks'] = dict(
        frequency=all(abs(rate / (expected if name in ('low', 'out') else expected / 2) - 1) <= 500e-6
                      for name, rate in rates.items()),
        phase_wander=result['divided_phase_wander_s'] <= 80e-12,
        cycle_variation=max(periods) - min(periods) <= 80e-12,
        delivery=0 <= min(delays) <= max(delays) <= 1e-9,
        output_rails=min(high) > 2.8 and max(low) < .33,
        supplies=result['logic_supply_min_V'] >= 1.1 and result['io_supply_min_V'] >= 3.05,
        control=.35 < min(control) <= max(control) < 1.0 and max(control) - min(control) <= .002,
        bounded_nodes=all(-.1 < row[col] < 3.45 for row in steady for col in (2, 3, 9)))
    result['status'] = 'pass' if all(result['checks'].values()) else 'fail'
    return result
