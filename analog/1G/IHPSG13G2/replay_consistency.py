"""Check that the final PSP trajectory reproduces every causal feedback read."""
import bisect
import csv
import json
import math


def check_replayed_decisions(work, count, tolerance=.001):
    with (work / 'columns.csv').open() as stream:
        header = next(csv.reader(stream))
    with (work / 'spice.dat').open() as stream:
        names = next(stream).split()
        if len(names) != len(header) or names[0] != 'time':
            raise ValueError('changed replay trajectory interface')
        rows = [[float(x) for x in line.split()] for line in stream if line.strip()]
    if (len(rows) < 2 or any(len(row) != len(header) or not all(math.isfinite(x) for x in row) for row in rows)):
        raise ValueError('incomplete replay trajectory')
    times = [row[0] for row in rows]
    if any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError('unordered replay trajectory')
    with (work / 'controls.csv').open() as stream:
        controls = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]
    decisions = json.loads((work / 'decisions.json').read_text())
    metadata = json.loads((work / 'live.json').read_text())
    clocks, period = metadata['clocks'], metadata['period_s']
    if (count != metadata['slices'] or len(controls) != clocks + 1 or len(decisions) != clocks
            or [row['clock'] for row in controls] != list(range(-1, clocks))
            or abs(times[-1] - clocks * period) > 1e-14):
        raise ValueError('incomplete replay controller coverage')
    low, high = metadata['feedback_low_V'], metadata['feedback_high_V']
    if not 0 < low < high or not math.isfinite(tolerance) or not 0 < tolerance <= .001:
        raise ValueError('invalid replay voltage contract')
    checked, worst = 0, 0.0
    for clock, decision in enumerate(decisions):
        mask = controls[clock]['convert'] & ((1 << count) - 1)
        time = (clock + .5) * period
        if (decision['clock'] != clock or abs(decision['time_s'] - time) > 1e-15
                or decision['sampled'] != bool(mask) or len(decision['voltage_V']) != count):
            raise ValueError('changed replay feedback schedule')
        if not mask:
            if any(x is not None for x in decision['voltage_V']) or decision['feedback'] != 0:
                raise ValueError('unsampled replay decisions must be explicit')
            continue
        feedback = 0
        index = bisect.bisect_left(times, time)
        if not 0 < index < len(rows):
            raise ValueError('replay feedback outside final trajectory')
        a, b = rows[index - 1], rows[index]
        for slice_index in range(count):
            if not mask & (1 << slice_index):
                continue
            recorded = decision['voltage_V'][slice_index]
            column = header.index('CORE_DECISION' + str(slice_index))
            achieved = a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])
            if (recorded is None or not math.isfinite(recorded) or low <= recorded <= high
                    or low <= achieved <= high or (recorded > high) != (achieved > high)):
                raise ValueError('final trajectory disagrees with a resolved replay decision')
            worst = max(worst, abs(recorded - achieved))
            feedback |= int(achieved > high) << slice_index
            checked += 1
        if feedback != decision['feedback']:
            raise ValueError('replay feedback bits disagree with their electrical observations')
    if checked == 0 or worst > tolerance:
        raise ValueError('replayed electrical decision voltage exceeds its consistency tolerance')
    return dict(decision_reads=checked, maximum_decision_replay_difference_V=worst,
                decision_replay_tolerance_V=tolerance)
