#!/usr/bin/env python3
"""Compare qualified oscillator flows without assuming equal free-running phase."""
import argparse
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path

from processing_clock_oscillator import CASES, HEADER, measure, digest
from compare import checked_rows, edges, interpolate

SOURCE = Path(__file__).resolve()


def read_trace(path, flow, expected_hash):
    checksum = hashlib.sha256()
    with gzip.open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(block)
    if checksum.hexdigest() != expected_hash:
        raise ValueError('retained oscillator waveform changed')
    with gzip.open(path, 'rt') as stream:
        if flow == 'cpp':
            reader = csv.reader(stream)
            if next(reader) != HEADER:
                raise ValueError('changed native observation interface')
        else:
            names = next(stream).split()
            if len(names) != len(HEADER) or names[0] != 'time':
                raise ValueError('changed SPICE observation interface')
            reader = (line.split() for line in stream if line.strip())
        return checked_rows(reader, len(HEADER), str(path))


def period_shapes(rows):
    crossings = edges(rows, 1, .6, 256e-9, 512e-9)
    if len(crossings) < 9:
        raise ValueError('insufficient complete periods for waveform comparison')
    times = [r[0] for r in rows]
    # Align each complete late period at its own rising edge. Comparing raw
    # timestamps would mostly measure accumulated free-running phase drift.
    shapes = []
    for column in (1, 2):
        shapes.append([sum(interpolate(rows, times, a + (b - a) * i / 128, column)
                           for a, b in zip(crossings[-9:-1], crossings[-8:])) / 8
                       for i in range(128)])
    return shapes


def evaluate(cpp, veriloga):
    input_hashes = {str(p.resolve()): digest(p) for p in (SOURCE, cpp, veriloga)}
    reports = [json.loads(p.read_text()) for p in (cpp, veriloga)]
    for report, flow in zip(reports, ('cpp', 'veriloga')):
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report.get('cases', {})) != set(CASES):
            raise ValueError('missing independently passed oscillator flow: ' + flow)
        if not report.get('source_sha256') or any(digest(Path(p)) != h for p, h in report['source_sha256'].items()):
            raise ValueError('stale oscillator source/tool evidence')
    roots = [Path(report['work']) for report in reports]
    results = {}
    for name, (trim, load, refined) in CASES.items():
        for file in ('design.va', 'bench.cir', 'columns.csv'):
            if digest(roots[0] / name / file) != digest(roots[1] / name / file):
                raise ValueError('different oscillator experiments: ' + name + '/' + file)
        metrics, shapes = [], []
        for root, report, flow in zip(roots, reports, ('cpp', 'veriloga')):
            recorded = report['cases'][name]
            if recorded['parameters'] != dict(trim=trim, load_F=load, refined=refined):
                raise ValueError('changed trim/load/refinement case')
            path = root / name / ('cpp/trace.csv.gz' if flow == 'cpp' else 'spice.dat.gz')
            rows = read_trace(path, flow, recorded['trace_sha256'])
            measured = measure(rows, refined)
            if measured['status'] != 'pass' or any(recorded.get(k) != v for k, v in measured.items()):
                raise ValueError('retained waveform fails independent electrical checks')
            metrics.append(measured)
            shapes.append(period_shapes(rows))
        a, b = metrics
        frequency = abs(a['frequency_Hz'] / b['frequency_Hz'] - 1)
        waveform = [math.sqrt(sum((x - y) ** 2 for x, y in zip(left, right)) / 128)
                    for left, right in zip(*shapes)]
        control = abs(a['control_mean_V'] - b['control_mean_V'])
        supply = abs(a['supply_min_V'] - b['supply_min_V'])
        passed = frequency <= (.005 if refined else .01) and max(waveform) <= .06 and control <= .002 and supply <= .005
        results[name] = dict(status='pass' if passed else 'fail', frequency_relative_difference=frequency,
                             period_normalized_rms_V=dict(zip(('OUT', 'RING'), waveform)),
                             control_difference_V=control, supply_difference_V=supply)
    # Halving the C++ integration step must reduce the frequency discrepancy,
    # in addition to satisfying each solver's absolute electrical contract.
    convergence = results['refined']['frequency_relative_difference'] <= max(
        .0001, .65 * results['38528']['frequency_relative_difference'])
    passed = convergence and all(r['status'] == 'pass' for r in results.values())
    evidence = dict(status='pass' if passed else 'fail', cases=results, refinement_converges=convergence,
                    limits=dict(frequency_relative=.01, refined_frequency_relative=.005,
                                period_normalized_rms_V=.06, control_V=.002, supply_V=.005),
                    input_sha256=input_hashes,
                    scope='same-equation oscillator comparison; independent PSP and extracted-layout checks remain required')
    for report in reports:
        if any(digest(Path(p)) != h for p, h in report['source_sha256'].items()):
            raise ValueError('oscillator source/tool changed during comparison')
    if any(digest(Path(p)) != h for p, h in input_hashes.items()):
        raise ValueError('oscillator reports changed during comparison')
    return evidence


def compare(cpp, veriloga, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    evidence = dict(status='running')
    output.write_text(json.dumps(evidence, indent=2) + '\n')
    try:
        evidence = evaluate(cpp, veriloga)
        if evidence['status'] != 'pass':
            raise ValueError('oscillator solvers disagree; see ' + str(output))
    except BaseException as error:
        evidence.update(status='fail', error=str(error))
        raise
    finally:
        output.write_text(json.dumps(evidence, indent=2) + '\n')
    print('PASS oscillator solvers: all trims, load, period shapes and refinement')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cpp', 'veriloga', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    compare(args.cpp, args.veriloga, args.output)
