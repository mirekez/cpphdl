#!/usr/bin/env python3
"""Compare the independently checked DAC on both simulators' observed grids."""
import argparse
import json
from pathlib import Path

from core_receive_dac import HEADER, contract, digest, read_states, verify


def directional_errors(observed, reference):
    maximum = [0.] * (len(observed[0]) - 1)
    position, count = 1, 0
    for row in observed:
        if row[0] < reference[0][0]:
            continue
        if row[0] > reference[-1][0] + 1e-15:
            raise ValueError('reference does not cover the comparison endpoint')
        while position < len(reference) - 1 and reference[position][0] < row[0]:
            position += 1
        before, after = reference[position - 1], reference[position]
        fraction = (row[0] - before[0]) / (after[0] - before[0])
        for column in range(1, len(row)):
            expected = before[column] + fraction * (after[column] - before[column])
            maximum[column - 1] = max(maximum[column - 1], abs(row[column] - expected))
        count += 1
    if count < 10:
        raise ValueError('insufficient overlapping waveform observations')
    return maximum, count


def compare(work):
    rows, receipts = [], {}
    for flow in ('cpp', 'veriloga'):
        path = work / flow / 'checks.json'
        report = json.loads(path.read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow:
            raise ValueError('independently passing DAC flows required')
        if not report.get('source_sha256') or any(digest(p) != h for p, h in report['source_sha256'].items()):
            raise ValueError('stale DAC source or tool evidence')
        raw = Path(report['waveform'])
        if digest(raw) != report['waveform_sha256']:
            raise ValueError('changed DAC waveform')
        trajectory = read_states(raw, HEADER, csv_format=flow == 'cpp')
        absolute = contract(trajectory)
        verify(absolute)
        if any(report.get(k) != v for k, v in absolute.items()):
            raise ValueError('changed independent DAC acceptance results')
        rows.append(trajectory)
        receipts[flow] = digest(path)
    forward, native_count = directional_errors(rows[0], rows[1])
    reverse, spice_count = directional_errors(rows[1], rows[0])
    maximum = [max(a, b) for a, b in zip(forward, reverse)]
    passed = max(maximum) <= .005
    result = dict(status='pass' if passed else 'fail', tolerance_V=.005,
        maximum_differences_V=dict(zip(HEADER[1:], maximum)),
        cpp_grid_maximum_differences_V=dict(zip(HEADER[1:], forward)),
        veriloga_grid_maximum_differences_V=dict(zip(HEADER[1:], reverse)),
        compared_observations=dict(cpp=native_count, veriloga=spice_count),
        receipt_sha256=receipts, checker_sha256=digest(__file__),
        scope='linear interpolation on both observed grids over their common measurement interval; both absolute contracts independently rechecked')
    (work / 'comparison_both_grids.json').write_text(json.dumps(result, indent=2) + '\n')
    if not passed:
        raise ValueError('DAC waveforms differ by more than 5 mV on an observed grid')
    print('PASS DAC comparison on both grids; maximum difference V', max(maximum))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    compare(parser.parse_args().work)
