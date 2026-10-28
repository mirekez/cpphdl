#!/usr/bin/env python3
"""Compare independently qualified C++ and Verilog-A PLL trajectories."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / '1G/tests')]
from reference_clock_domain import read, digest
from reference_clock_contract import measure


def compare(cpp_path, va_path, destination):
    reports = [json.loads(path.read_text()) for path in (cpp_path, va_path)]
    for report, flow in zip(reports, ('cpp', 'veriloga')):
        if report.get('status') != 'pass' or report.get('flow') != flow:
            raise ValueError('missing independently passed clock flow: ' + flow)
        if not report.get('source_sha256') or any(digest(Path(p)) != h for p, h in report['source_sha256'].items()):
            raise ValueError('clock evidence has stale source/tool inputs')
    roots = [Path(report['work']) for report in reports]
    shared = reports[0]['source_sha256'].keys() & reports[1]['source_sha256'].keys()
    if any(reports[0]['source_sha256'][p] != reports[1]['source_sha256'][p] for p in shared):
        raise ValueError('clock flows consumed different shared sources/tools')
    # Equal generated experiments require the same sources, stimuli, output
    # loads and time grid. PSP uses different equations and is not substituted.
    for name in ('design.va', 'bench.cir', 'columns.csv'):
        if digest(roots[0] / name) != digest(roots[1] / name):
            raise ValueError('different electrical experiments: ' + name)
    metrics = []
    for root, report, flow in zip(roots, reports, ('cpp', 'veriloga')):
        trace = root / ('cpp/trace.csv.gz' if flow == 'cpp' else 'spice.dat.gz')
        checksum = hashlib.sha256()
        with gzip.open(trace, 'rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                checksum.update(block)
        if checksum.hexdigest() != report['trace_sha256']:
            raise ValueError('retained clock waveform changed')
        rows = read(trace, flow)
        result = measure(rows)
        if result != report['electrical'] or result['status'] != 'pass':
            raise ValueError('retained trajectory differs from independent electrical checks')
        metrics.append(result)
    a, b = metrics
    frequency_ppm = max(abs(a['frequency_Hz'][name] / b['frequency_Hz'][name] - 1) * 1e6
                        for name in a['frequency_Hz'])
    control = abs(a['control_mean_V'] - b['control_mean_V'])
    phase = abs(a['divided_phase_mean_s'] - b['divided_phase_mean_s'])
    delivery = max(abs(a[key] - b[key]) for key in ('delivery_min_s', 'delivery_max_s'))
    supply = max(abs(a[key] - b[key]) for key in ('logic_supply_min_V', 'io_supply_min_V'))
    # Limits accompany the independent 500 ppm / 80 ps absolute requirements.
    # Settled phase, control and supply agreement also constrain false matches
    # that would be hidden by comparing only the average output frequency.
    checks = dict(frequency=frequency_ppm <= 100, control=control <= .002,
                  phase=phase <= 50e-12, delivery=delivery <= 50e-12, supplies=supply <= .005)
    report = dict(status='pass' if all(checks.values()) else 'fail', checks=checks,
                  frequency_difference_ppm=frequency_ppm, control_difference_V=control,
                  phase_difference_s=phase, delivery_difference_s=delivery, supply_difference_V=supply,
                  limits=dict(frequency_ppm=100, control_V=.002, phase_s=50e-12,
                              delivery_s=50e-12, supply_V=.005),
                  input_sha256={str(p.resolve()): digest(p) for p in (SOURCE, cpp_path, va_path)},
                  scope='same-equation schematic solver comparison; PSP and extracted layout require separate qualification')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + '\n')
    if report['status'] != 'pass':
        raise ValueError('clock solver disagreement: ' + str(checks))
    print('PASS clock solvers: %.3f ppm, %.3f mV control, %.3f ps phase' %
          (frequency_ppm, control * 1000, phase * 1e12))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cpp', 'veriloga', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    compare(args.cpp, args.veriloga, args.output)
