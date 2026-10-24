#!/usr/bin/env python3
"""Measure return timing from an independently passing electrical trajectory."""
import argparse
from bisect import bisect_left
import json
from pathlib import Path

from core_receive_decision import CODES, HEADER, PERIOD, WARMUP, contract, digest, verify
from trace_states import read_states


def return_timing(rows):
    times = [row[0] for row in rows]
    records = []
    for index, code in enumerate(CODES):
        begin = WARMUP + index * PERIOD
        left = bisect_left(times, begin)
        right = bisect_left(times, begin + PERIOD)
        selected = rows[left:right]
        if len(selected) < 2:
            raise ValueError('incomplete return timing interval')
        above = (index % 2 == 0) != (index >= 8)
        valid = [row[10] >= .84 if above else row[10] <= .36 for row in selected]
        if not valid[-1]:
            raise ValueError('return does not remain valid through the trial endpoint')
        wrong = [position for position, value in enumerate(valid) if not value]
        settled = selected[wrong[-1] + 1][0] - begin if wrong else 0.
        crossings = []
        for a, b in zip(selected, selected[1:]):
            before, after = a[10] >= a[12] / 2, b[10] >= b[12] / 2
            if before != after:
                crossings.append(dict(direction='rise' if after else 'fall',
                    bracket_s=[a[0] - begin, b[0] - begin]))
        records.append(dict(trial=index, code=code, expected_high=above,
            already_valid_at_start=valid[0], remains_valid_from_s=settled,
            core_half_rail_crossings=crossings))
    return records


def characterize(receipt, output):
    proof = json.loads(receipt.read_text())
    if proof.get('status') != 'pass' or proof.get('flow') not in ('cpp', 'veriloga', 'psp'):
        raise ValueError('passing connected decision evidence required')
    hashes = proof.get('source_sha256', {})
    if not hashes or any(digest(path) != value for path, value in hashes.items()):
        raise ValueError('connected decision source evidence changed')
    waveform = Path(proof['waveform'])
    if digest(waveform) != proof['waveform_sha256']:
        raise ValueError('connected decision waveform changed')
    rows = read_states(waveform, HEADER, csv_format=proof['flow'] == 'cpp')
    absolute = contract(rows)
    verify(absolute)
    if any(proof.get(name) != value for name, value in absolute.items()):
        raise ValueError('connected decision results changed')
    records = return_timing(rows)
    result = dict(status='characterized', flow=proof['flow'], corner=proof['corner'],
        temperature_C=proof['temperature_C'], trials=records,
        maximum_remains_valid_from_s=max(record['remains_valid_from_s'] for record in records),
        input_receipt=str(receipt.resolve()), input_receipt_sha256=digest(receipt),
        waveform_sha256=proof['waveform_sha256'], checker_sha256=digest(Path(__file__)),
        scope='observed return timing relative to each local trial command; half-rail crossings are time brackets; unchanged words are not new transitions; no routed-clock, mismatch, jitter or noise margin is inferred')
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('use a new output path to preserve earlier characterization')
    result = characterize(args.receipt, args.output)
    print('Maximum observed stable return:', result['maximum_remains_valid_from_s'], 's')
