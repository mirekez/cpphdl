#!/usr/bin/env python3
"""Check a physical high-to-core voltage interface before comparing solvers."""
import argparse
import bisect
import csv
import gzip
import hashlib
import itertools
import json
import math
from pathlib import Path
import shutil
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import checked_rows, read_cpp, read_spice
from hardware import configure_cleanup, execute, source_dependencies

HEADER = ['time_s', 'Y', 'INVERTED', 'A', 'VDD', 'COMMAND']
CASES = {f'{core}_{high}_{load}': (core, high, load, 2)
         for core, high, load in itertools.product((1.08, 1.2, 1.32), (2.97, 3.3, 3.63), (20, 200))}
CASES.update(overload=(1.2, 3.3, 5000, 2), refined=(1.2, 3.3, 200, 1))


def digest(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def contract(rows, core, high):
    # UIC ngspice starts at its first accepted integration point; native also
    # records the initial state. Both must cover the first maximum timestep.
    if not rows or not 0 <= rows[0][0] <= 2e-12 or abs(rows[-1][0] - 32e-9) > 1e-16:
        raise ValueError('incomplete voltage-interface trajectory')
    if any(len(row) != len(HEADER) or not all(math.isfinite(x) for x in row) for row in rows):
        raise ValueError('invalid voltage-interface observations')
    times = [row[0] for row in rows]
    if any(not 0 < b - a <= 2.01e-12 for a, b in zip(times, times[1:])):
        raise ValueError('voltage-interface times are unordered or undersampled')

    def at(time, column):
        i = min(max(1, bisect.bisect_left(times, time)), len(rows) - 1)
        a, b = rows[i - 1], rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    # Every edge must deliver a valid rail by 1 ns, then retain it to the next
    # transition. Dense plateau probes catch droop after an apparent crossing.
    low, top = [], []
    for symbol in range(2, 16):
        for phase in (1e-9, 1.25e-9, 1.5e-9, 1.9e-9):
            (top if symbol & 1 else low).append(at(symbol * 2e-9 + phase, 1))
    steady = [r for r in rows if r[0] >= 4e-9]
    exposure = max(max(abs(r[2]), abs(r[2] - r[4]), abs(r[1]), abs(r[1] - r[4])) for r in steady)
    checks = dict(output_rails=max(low) < .1 * core and min(top) > .85 * core,
                  core_gate_exposure=exposure < 1.65,
                  core_supply=min(r[4] for r in steady) > .85 * core,
                  finite_supply_current=1e-5 < core - min(r[4] for r in steady) < .15 * core,
                  bounded_nodes=min(min(r[1:5]) for r in steady) > -.15 and
                                max(max(r[1:3]) for r in steady) < core + .15,
                  finite_input_charge=max(abs(r[3] - r[5]) for r in steady) > 1e-5,
                  input_delivered=all(abs(at(i * 2e-9 + 1e-9, 3) - (high if i & 1 else 0)) < .02
                                      for i in range(2, 16)))
    edges = []
    for a, b in zip(steady, steady[1:]):
        if a[1] < core / 2 <= b[1] or b[1] < core / 2 <= a[1]:
            edges.append([int(b[1] > a[1]), a[0] + (b[0] - a[0]) * (core / 2 - a[1]) / (b[1] - a[1])])
    return dict(checks=checks, highest_low_V=max(low), lowest_high_V=min(top),
                minimum_supply_V=min(r[4] for r in steady), maximum_core_exposure_V=exposure,
                output_edges=edges,
                plateau_V=[at(i * 2e-9 + p, 1) for i in range(2, 16) for p in (1e-9, 1.5e-9, 1.9e-9)])


def verify(result, overloaded=False):
    expected = {key: not (overloaded and key == 'output_rails') for key in result['checks']}
    if result['checks'] != expected:
        raise ValueError('voltage-interface absolute contract failed: ' + str(result['checks']))


def read_saved(path, flow):
    with gzip.open(path, 'rt') as file:
        if flow == 'cpp':
            rows = csv.reader(file)
            if next(rows) != HEADER:
                raise ValueError('changed voltage-interface columns')
        else:
            if len(next(file).split()) != len(HEADER):
                raise ValueError('changed voltage-interface SPICE columns')
            rows = (line.split() for line in file if line.strip())
        return checked_rows(rows, len(HEADER), str(path))


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    for path in (SOURCE, ANALOG / 'VerilogA/compare.py'):
        hashes[str(path)] = digest(path)
    binaries = {str(args.bench.resolve()): digest(args.bench)}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p) for p in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=hashes, binary_sha256=binaries,
                  scope='four-MOS high-to-core voltage interface with explicit charge; generic laws, no layout or power-off claim')
    try:
        for name, (core, high, load, step) in CASES.items():
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'bench.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                         str(core), str(high), str(load), str(step)], stdout=log, stderr=-2, check=True, timeout=180)
            if args.flow == 'cpp':
                raw = case / 'cpp/trace.csv'
                header, rows = read_cpp(raw)
                if header != HEADER:
                    raise ValueError('voltage-interface observations changed')
            else:
                for label, command in (('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                                       ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])):
                    with (case / (label + '.log')).open('w') as log:
                        execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=180)
                raw = case / 'spice.dat'
                rows = read_spice(raw, HEADER)
            result = contract(rows, core, high)
            verify(result, name == 'overload')
            archive = raw.with_suffix(raw.suffix + '.gz')
            raw_sha = digest(raw)
            with raw.open('rb') as original, gzip.open(archive, 'wb', compresslevel=1) as output:
                shutil.copyfileobj(original, output)
            with gzip.open(archive, 'rb') as original:
                if hashlib.file_digest(original, 'sha256').hexdigest() != raw_sha:
                    raise ValueError('voltage-interface archive is not lossless')
            raw.unlink()
            result.update(waveform=str(archive), waveform_sha256=digest(archive))
            report['cases'][name] = result
            (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        nominal, refined = [report['cases'][k]['plateau_V'] for k in ('1.2_3.3_200', 'refined')]
        report['maximum_refinement_V'] = max(abs(a - b) for a, b in zip(nominal, refined))
        if report['maximum_refinement_V'] > .005:
            raise ValueError('voltage-interface refinement exceeds 5 mV')
        if any(digest(p) != value for p, value in (hashes | binaries).items()):
            raise ValueError('voltage-interface proof inputs changed during execution')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    work = args.work.resolve()
    (work / 'comparison.json').unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        report = json.loads((work / flow / 'checks.json').read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != set(CASES):
            raise ValueError('both complete voltage-interface flows must pass independently')
        if any(digest(p) != value for p, value in (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale voltage-interface proof')
        for name, result in report['cases'].items():
            if digest(result['waveform']) != result['waveform_sha256']:
                raise ValueError('changed voltage-interface waveform')
            current = contract(read_saved(result['waveform'], flow), *CASES[name][:2])
            verify(current, name == 'overload')
            if any(result[k] != value for k, value in current.items()):
                raise ValueError('voltage-interface absolute evidence changed')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('voltage-interface flows used different sources')
    differences = {}
    for name in CASES:
        a, b = [r['cases'][name] for r in reports]
        if len(a['output_edges']) != len(b['output_edges']) or any(x[0] != y[0] for x, y in zip(a['output_edges'], b['output_edges'])):
            raise ValueError('voltage-interface edge sequence differs: ' + name)
        edge = max((abs(x[1] - y[1]) for x, y in zip(a['output_edges'], b['output_edges'])), default=0)
        plateau = max(abs(x - y) for x, y in zip(a['plateau_V'], b['plateau_V']))
        if edge > 10e-12 or plateau > .005:
            raise ValueError('voltage-interface solvers disagree: ' + name)
        differences[name] = dict(maximum_edge_difference_s=edge, maximum_plateau_difference_V=plateau)
    (work / 'comparison.json').write_text(json.dumps(dict(status='pass', cases=differences,
        flow_report_sha256={flow: digest(work / flow / 'checks.json') for flow in ('cpp', 'veriloga')}), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    compare(args) if args.flow == 'compare' else run(args)
