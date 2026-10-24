#!/usr/bin/env python3
"""Check follower KCL, delivery and loading before comparing electrical solvers."""
import argparse
import bisect
import csv
import gzip
import hashlib
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

HEADER = ['time_s', 'OUT', 'IN', 'VDD', 'COMMAND']
LEVELS = [code for first in range(5) for second in range(5) for code in (first, second)]
CASES = {f'{supply}_{load}': (supply, load, 10000, 5)
         for supply in (2.97, 3.3, 3.63) for load in (.05, 4)}
CASES.update(overload=(3.3, 40, 10000, 5), resistive_load=(3.3, 4, 100, 5), refined=(3.3, 4, 10000, 2.5))


def digest(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def settled(input_voltage, load_resistance):
    # Independent saturation-KCL solution: beta/2*(Vin - Vout - Vth)^2
    # supplies Vout*(1/Rbias + 1/Rload). Use the smaller physical root.
    overdrive = max(0, input_voltage - .7)
    term = 2 * (1 / 500 + 1 / load_resistance) / .05
    return 2 * overdrive ** 2 / (2 * overdrive + term + math.sqrt(term ** 2 + 4 * overdrive * term))


def contract(rows, supply, load_resistance, targets=None):
    if not rows or not 0 <= rows[0][0] <= 5e-12 or abs(rows[-1][0] - 204e-9) > 1e-15:
        raise ValueError('incomplete follower trajectory')
    if any(len(r) != len(HEADER) or not all(math.isfinite(x) for x in r) for r in rows):
        raise ValueError('invalid follower observations')
    times = [r[0] for r in rows]
    if any(not 0 < b - a <= 5.01e-12 for a, b in zip(times, times[1:])):
        raise ValueError('unordered or undersampled follower waveform')

    def at(time, column):
        i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[i - 1], rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    delivery, dc, plateaus = [], [], []
    for index, code in enumerate(LEVELS):
        start = (4 + 4 * index) * 1e-9
        target = settled(2.5 + .2 * (code - 2), load_resistance) if targets is None else targets[code]
        delivery.append(abs(at(start + 1.5e-9, 1) - target))
        dc.append(abs(at(start + 3.8e-9, 1) - target))
        plateaus.append(at(start + 3.8e-9, 1))
    steady = [r for r in rows if r[0] >= 4e-9]
    checks = dict(settling=max(delivery) < .02, dc_accuracy=max(dc) < .001,
                  output_headroom=all(.2 < r[1] < r[3] - .3 for r in steady),
                  saturation=all(r[3] > r[2] - .7 for r in steady),
                  finite_supply=.85 * supply < min(r[3] for r in steady) < supply - .001,
                  bounded_input=min(r[2] for r in steady) > 2 and max(r[2] for r in steady) < 3,
                  finite_input_charge=max(abs(r[2] - r[4]) for r in steady) > 1e-5)
    return dict(checks=checks, maximum_delivery_error_V=max(delivery), maximum_dc_error_V=max(dc),
                minimum_supply_V=min(r[3] for r in steady), plateau_V=plateaus,
                waveform_probes_V=[at((4 + .05 * i) * 1e-9, 1) for i in range(4000)])


def verify(result, overloaded=False):
    # The overload is required to expose failed delivery. Its measured DC
    # error is retained, but no settled-plateau guarantee applies to that case.
    for name, passed in result['checks'].items():
        if overloaded and name == 'dc_accuracy':
            continue
        if passed != (not overloaded or name != 'settling'):
            raise ValueError('follower contract failed: ' + str(result['checks']))


def read_saved(path, flow):
    with gzip.open(path, 'rt') as file:
        if flow == 'cpp':
            rows = csv.reader(file)
            if next(rows) != HEADER:
                raise ValueError('changed follower observation columns')
        else:
            if len(next(file).split()) != len(HEADER):
                raise ValueError('changed follower SPICE columns')
            rows = (line.split() for line in file if line.strip())
        return checked_rows(rows, len(HEADER), str(path))


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    for path in (SOURCE, ANALOG / 'VerilogA/compare.py'):
        hashes[str(path)] = digest(path)
    binaries = {str(args.bench.resolve()): digest(args.bench)}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p) for p in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=hashes, binary_sha256=binaries,
                  scope='generic MOS source follower with resistor bias and explicit charge; no PSP or layout qualification')
    try:
        for name, (supply, load, return_load, step) in CASES.items():
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'bench.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                         str(supply), str(load), str(return_load), str(step)], stdout=log, stderr=-2, check=True, timeout=180)
            if args.flow == 'cpp':
                raw = case / 'cpp/trace.csv'
                header, rows = read_cpp(raw)
                if header != HEADER:
                    raise ValueError('changed follower observations')
            else:
                for label, command in (('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                                       ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])):
                    with (case / (label + '.log')).open('w') as log:
                        execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=180)
                raw = case / 'spice.dat'
                rows = read_spice(raw, HEADER)
            result = contract(rows, supply, return_load)
            verify(result, name == 'overload')
            archive = raw.with_suffix(raw.suffix + '.gz')
            original = digest(raw)
            with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as output:
                shutil.copyfileobj(source, output)
            with gzip.open(archive, 'rb') as source:
                if hashlib.file_digest(source, 'sha256').hexdigest() != original:
                    raise ValueError('follower waveform archive is not lossless')
            raw.unlink()
            result.update(waveform=str(archive), waveform_sha256=digest(archive))
            report['cases'][name] = result
            (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        nominal, refined = [report['cases'][key]['waveform_probes_V'] for key in ('3.3_4', 'refined')]
        report['maximum_refinement_V'] = max(abs(a - b) for a, b in zip(nominal, refined))
        if report['maximum_refinement_V'] > .01:
            raise ValueError('follower refinement exceeds 10 mV')
        if any(digest(p) != value for p, value in (hashes | binaries).items()):
            raise ValueError('follower proof inputs changed during execution')
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
            raise ValueError('both complete follower flows must pass independently')
        if any(digest(p) != value for p, value in (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale follower proof')
        for name, result in report['cases'].items():
            if digest(result['waveform']) != result['waveform_sha256']:
                raise ValueError('changed follower waveform')
            current = contract(read_saved(result['waveform'], flow), CASES[name][0], CASES[name][2])
            verify(current, name == 'overload')
            if any(result[k] != value for k, value in current.items()):
                raise ValueError('follower absolute evidence changed')
        refinement = max(abs(a - b) for a, b in zip(report['cases']['3.3_4']['waveform_probes_V'],
                                                  report['cases']['refined']['waveform_probes_V']))
        if refinement != report['maximum_refinement_V'] or refinement > .01:
            raise ValueError('invalid follower refinement evidence')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('follower flows tested different sources')
    differences = {}
    for name in CASES:
        a, b = [r['cases'][name] for r in reports]
        waveform = max(abs(x - y) for x, y in zip(a['waveform_probes_V'], b['waveform_probes_V']))
        plateau = max(abs(x - y) for x, y in zip(a['plateau_V'], b['plateau_V']))
        if waveform > .01 or plateau > .001:
            raise ValueError('follower solvers disagree: ' + name)
        differences[name] = dict(maximum_waveform_difference_V=waveform, maximum_plateau_difference_V=plateau)
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
