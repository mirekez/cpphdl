#!/usr/bin/env python3
"""Audit actual closed-controller masks, including every overlapping acquisition."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def check(rows, metadata):
    count, launches, clocks = (metadata[k] for k in ('slices', 'launches', 'clocks'))
    period = metadata['period_s']
    if (not 1 <= count <= 16 or launches < count or launches % count or
            not math.isclose(period, 4e-9, rel_tol=0, abs_tol=1e-20)):
        raise ValueError('unexpected controller configuration')
    if [row['clock'] for row in rows] != list(range(-1, clocks)):
        raise ValueError('missing, duplicate or reordered controller clock')
    maximum_overlap = 0
    for row in rows:
        clock = row['clock']
        acquire, convert = 0, 0
        for launch in range(launches):
            bit = 1 << (launch % count)
            if launch + 1 <= clock < launch + 3:
                acquire |= bit
            if launch + 4 <= clock < launch + 12:
                convert |= bit
        if row['fault'] or (row['acquire'], row['convert']) != (acquire, convert):
            raise ValueError('wrong acquisition/conversion masks or controller fault')
        if row['acquire'] & row['convert']:
            raise ValueError('sampling and conversion overlap within one slice')
        maximum_overlap = max(maximum_overlap, row['acquire'].bit_count())
    return dict(tracking_s=2 * period, sampling_to_reference_gap_s=period,
        conversion_s=8 * period, verified_acquisitions=launches,
        maximum_concurrently_tracking_slices=maximum_overlap)


def audit(receipt_path):
    report = json.loads(receipt_path.read_text())
    if report.get('status') != 'pass' or report.get('flow') not in ('cpp', 'veriloga', 'psp'):
        raise ValueError('a passing closed-conversion receipt is required')
    work = Path(report['work'])
    for name in ('live.json', 'controls.csv'):
        path = work / name
        if report['artifact_sha256'].get(str(path)) != digest(path):
            raise ValueError('controller artifact changed: ' + name)
    rows = [{key: int(value) for key, value in row.items()}
            for row in csv.DictReader((work / 'controls.csv').open())]
    result = check(rows, json.loads((work / 'live.json').read_text()))
    # Deliberately shorten one acquisition, assert an extra gate, and lose a
    # clock. Each must be rejected even if final conversion codes still match.
    shortened = [dict(row) for row in rows]
    shortened[3]['acquire'] &= ~1
    extra = [dict(row) for row in rows]
    extra[1]['acquire'] |= 1
    rejected = 0
    for invalid in (shortened, extra, rows[:3] + rows[4:]):
        try:
            check(invalid, json.loads((work / 'live.json').read_text()))
        except ValueError:
            rejected += 1
        else:
            raise ValueError('controller timing negative control was accepted')
    return result | dict(negative_controls_rejected=rejected, flow=report['flow'],
        receipt=str(receipt_path.resolve()), receipt_sha256=digest(receipt_path))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    results = [audit(path) for path in args.receipt]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(status='pass', checks=results,
        checker_sha256=digest(__file__)), indent=2) + '\n')
    print('PASS controller timing and negative controls:', len(results), 'flows; 8 ns tracking')
