#!/usr/bin/env python3
"""Absolute rail/settling/loading contracts followed by solver comparison."""
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
import statistics
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import read_cpp, read_spice, checked_rows
from hardware import execute, source_dependencies, configure_cleanup


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_saved(path, flow):
    header = ['time_s', 'Y', 'Y_N', 'A', 'VDD', 'VLOGIC', 'COMMAND']
    with gzip.open(path, 'rt') as file:
        if flow == 'cpp':
            reader = csv.reader(file)
            if next(reader) != header:
                raise ValueError('stored level-shifter observation interface changed')
        else:
            names = next(file).split()
            if len(names) != len(header) or names[0] != 'time':
                raise ValueError('stored level-shifter SPICE interface changed')
            reader = (line.split() for line in file if line.strip())
        return checked_rows(reader, len(header), str(path))


def contracts(rows, low, high, loaded, half_period=8e-9, startup=0):
    if startup:
        rows = [[row[0] - startup, *row[1:]] for row in rows if row[0] >= startup]
    if (not rows or abs(rows[-1][0] - 16 * half_period) > 1e-14 or
            any(len(row) != 7 or any(x is None or not math.isfinite(x) for x in row) for row in rows)):
        raise ValueError('incomplete/nonfinite level-shifter trajectory')
    times = [row[0] for row in rows]
    if any(b - a > min(32e-12, half_period / 256) * 1.001 for a, b in zip(times, times[1:])):
        raise ValueError('insufficient level-shifter waveform resolution')

    def at(time, column):
        index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[index - 1], rows[index]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    lows, highs, inverted_lows, inverted_highs = [], [], [], []
    phases = (2e-9, 4e-9, 7e-9) if half_period > 4e-9 else tuple(half_period * f for f in (.5, .75, .9))
    for symbol in range(2, 16):
        for phase in phases:
            y, yn = at(symbol * half_period + phase, 1), at(symbol * half_period + phase, 2)
            (highs if symbol & 1 else lows).append(y)
            (inverted_lows if symbol & 1 else inverted_highs).append(yn)
    probes = [[at(i * half_period / 64, column) for column in range(1, 7)] for i in range(128, 1024)]
    crossings = []
    for column in (1, 2):
        for a, b in zip(rows, rows[1:]):
            if a[0] > 2 * half_period and (a[column] < high / 2 <= b[column] or b[column] < high / 2 <= a[column]):
                crossings.append([column, int(b[column] > a[column]),
                                  a[0] + (b[0] - a[0]) * (high / 2 - a[column]) / (b[column] - a[column])])
    steady = [row for row in rows if row[0] > 2 * half_period]
    checks = dict(output_low=max(lows + inverted_lows) < .1 * high,
                  output_high=min(highs + inverted_highs) > .85 * high,
                  bounded_nodes=min(min(row[1:6]) for row in steady) > -.1,
                  bounded_overshoot=max(max(row[1:3]) for row in steady) < high + .15,
                  finite_supply_loading=1e-6 < high - min(row[4] for row in steady) < .3,
                  logic_domain_preserved=min(row[5] for row in steady) > .85 * low)
    if loaded:
        checks['load_current_from_supply'] = high - at(15 * half_period + phases[-1], 4) > .001
    return dict(checks=checks, lowest_high_V=min(highs + inverted_highs), highest_low_V=max(lows + inverted_lows),
                largest_supply_droop_V=high - min(row[4] for row in steady), comparison_probes_V=probes,
                output_crossings=crossings)


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    provenance = source_dependencies([SOURCE.parent / 'level_shifter.cpp'], ANALOG.parent, args.cxx,
                                    ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    provenance[str(SOURCE)] = digest(SOURCE)
    provenance[str(ANALOG / 'VerilogA/compare.py')] = digest(ANALOG / 'VerilogA/compare.py')
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p.resolve()) for p in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=provenance,
                  clock_MHz=args.clock_mhz,
                  topology='clock cascode with 200 fF output correction' if args.cascoded else 'direct HV pull-down',
                  binary_sha256=binaries,
                  scope='ten-transistor voltage interface; generic laws, not an IHP fit or extracted layout')
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    for low, high, loaded in itertools.product((1.08, 1.2, 1.32), (2.97, 3.3, 3.63), (False, True)):
        name = f'{low}_{high}_{int(loaded)}'
        case = work / name
        case.mkdir(exist_ok=True)
        with (case / 'bench.log').open('w') as log:
            execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                     str(low), str(high), str(int(loaded)), str(args.clock_mhz),
                     'cascoded' if args.cascoded else 'direct'], stdout=log, stderr=-2, check=True, timeout=120)
        if args.flow == 'veriloga':
            with (case / 'compile.log').open('w') as log:
                execute([str(args.openvaf.resolve()), 'design.va', '-o', 'design.osdi'], cwd=case,
                        stdout=log, stderr=-2, check=True, timeout=120)
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'], cwd=case,
                        stdout=log, stderr=-2, check=True, timeout=120)
        with (case / 'columns.csv').open() as file:
            header = next(csv.reader(file))
        if args.flow == 'cpp':
            actual, rows = read_cpp(case / 'cpp/trace.csv')
            if actual != header:
                raise ValueError('level-shifter observation interface differs')
            trace = case / 'cpp/trace.csv'
        else:
            rows = read_spice(case / 'spice.dat', header)
            trace = case / 'spice.dat'
        result = contracts(rows, low, high, loaded, .5e-6 / args.clock_mhz, 32e-9 if args.cascoded else 0)
        compressed = trace.with_suffix(trace.suffix + '.gz')
        original_sha = digest(trace)
        with trace.open('rb') as original, gzip.open(compressed, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(original, output)
        with gzip.open(compressed, 'rb') as original:
            if hashlib.file_digest(original, 'sha256').hexdigest() != original_sha:
                raise ValueError('level-shifter waveform changed during archival')
        trace.unlink()
        result.update(waveform=str(compressed), waveform_sha256=digest(compressed),
                      low_V=low, high_V=high, loaded=loaded)
        report['cases'][name] = result
        report_path.write_text(json.dumps(report, indent=2) + '\n')
        if not all(result['checks'].values()):
            raise ValueError(name + ': ' + str(result['checks']))
        print(name + ': pass', flush=True)
    if any(digest(Path(path)) != value for path, value in (provenance | binaries).items()):
        raise ValueError('level-shifter sources changed while validation was running')
    report['status'] = 'pass'
    report_path.write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    (args.work.parent / 'comparison.json').unlink(missing_ok=True)
    reports = [json.loads((path / 'checks.json').read_text()) for path in (args.work, args.other)]
    if ({r.get('flow') for r in reports} != {'cpp', 'veriloga'} or
            any(r.get('status') != 'pass' for r in reports) or reports[0]['cases'].keys() != reports[1]['cases'].keys()):
        raise ValueError('both independent voltage-interface flows must pass first')
    if any(r.get('clock_MHz') != args.clock_mhz for r in reports):
        raise ValueError('voltage-interface comparisons require the same declared clock rate')
    if any(r.get('topology') != ('clock cascode with 200 fF output correction' if args.cascoded else 'direct HV pull-down') for r in reports):
        raise ValueError('voltage-interface comparisons require the same declared topology')
    expected = {f'{low}_{high}_{int(loaded)}' for low, high, loaded in
                itertools.product((1.08, 1.2, 1.32), (2.97, 3.3, 3.63), (False, True))}
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('voltage-interface flows tested different sources')
    for report in reports:
        if set(report['cases']) != expected:
            raise ValueError('voltage-interface coverage is incomplete')
        if any(digest(Path(path)) != value for path, value in
               (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale voltage-interface proof inputs')
        for result in report['cases'].values():
            path = Path(result['waveform'])
            if digest(path) != result['waveform_sha256']:
                raise ValueError('stored voltage-interface waveform changed')
            current = contracts(read_saved(path, report['flow']), result['low_V'], result['high_V'],
                                result['loaded'], .5e-6 / args.clock_mhz, 32e-9 if args.cascoded else 0)
            if any(result[key] != value for key, value in current.items()) or not all(current['checks'].values()):
                raise ValueError('voltage-interface absolute contract is no longer valid')
    results = []
    for name, first in reports[0]['cases'].items():
        second = reports[1]['cases'][name]
        delta = [abs(x - y) for a, b in zip(first['comparison_probes_V'], second['comparison_probes_V'])
                 for x, y in zip(a, b)]
        rms = math.sqrt(statistics.mean(x * x for x in delta))
        edges = first['output_crossings'], second['output_crossings']
        if len(edges[0]) != len(edges[1]) or any(a[:2] != b[:2] for a, b in zip(*edges)):
            raise ValueError('voltage-interface edge sequence differs in ' + name)
        timing = max(abs(a[2] - b[2]) for a, b in zip(*edges))
        # Steep regenerative edges are compared in time as well as whole-wave
        # RMS; an instantaneous voltage maximum is ill-conditioned there.
        if rms > .02 or timing > 10e-12:
            raise ValueError('voltage-interface solvers disagree in ' + name)
        results.append(dict(case=name, waveform_rms_V=rms, maximum_departure_V=max(delta), maximum_edge_departure_s=timing))
    (args.work.parent / 'comparison.json').write_text(json.dumps(dict(status='pass', cases=results), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--other', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--clock-mhz', type=float, choices=(62.5, 250), default=62.5)
    parser.add_argument('--cascoded', action='store_true')
    args = parser.parse_args()
    try:
        compare(args) if args.flow == 'compare' else run(args)
    except BaseException as error:
        path = args.work.resolve() / 'checks.json'
        if args.flow != 'compare' and path.is_file():
            result = json.loads(path.read_text())
            result.update(status='fail', error=str(error))
            path.write_text(json.dumps(result, indent=2) + '\n')
        raise
