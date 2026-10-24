#!/usr/bin/env python3
"""Independent oscillator contracts, refinement and phase-normalized comparison."""
import argparse
import bisect
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import shutil
import statistics
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import read_cpp, read_spice
from hardware import execute, configure_cleanup, source_dependencies

CASES = dict(low=(112, 0, 1, 128), nominal=(128, 0, 1, 128), high=(144, 0, 2, 128),
             loaded=(128, 1, 1, 128), refined=(128, 0, 2, 128),
             fine_low=(128, 0, 1, 96), fine_high=(128, 0, 1, 160))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    provenance = source_dependencies([SOURCE.parent / 'clock_oscillator.cpp'], ANALOG.parent, args.cxx,
                                    ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    provenance[str(SOURCE)] = digest(SOURCE)
    result = dict(status='running', flow=args.flow, cases={}, source_sha256=provenance,
                  scope='generic MOS electrical actuator; no closed receive loop, IHP fit or routed-clock qualification')
    report_path.write_text(json.dumps(result, indent=2) + '\n')
    for name, parameters in CASES.items():
        case = work / name
        case.mkdir(exist_ok=True)
        with (case / 'bench.log').open('w') as log:
            execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                     *map(str, parameters)], stdout=log, stderr=-2, check=True, timeout=600)
        if args.flow == 'veriloga':
            with (case / 'compile.log').open('w') as log:
                execute([str(args.openvaf.resolve()), 'design.va', '-o', 'design.osdi'], cwd=case,
                        stdout=log, stderr=-2, check=True, timeout=120)
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'], cwd=case,
                        stdout=log, stderr=-2, check=True, timeout=600)
        with (case / 'columns.csv').open() as file:
            header = next(csv.reader(file))
        if args.flow == 'cpp':
            actual, rows = read_cpp(case / 'cpp/trace.csv')
            if actual != header:
                raise ValueError('oscillator observation interface differs')
            trace = case / 'cpp/trace.csv'
        else:
            rows = read_spice(case / 'spice.dat', header)
            trace = case / 'spice.dat'
        if (not rows or abs(rows[-1][0] - 512e-9) > 1e-14 or
                any(x is None or not math.isfinite(x) for row in rows for x in row)):
            raise ValueError('incomplete/nonfinite clock trajectory')
        times = [row[0] for row in rows]
        if any(b - a > 32e-12 for a, b in zip(times, times[1:])):
            raise ValueError('clock waveform has insufficient time resolution')

        def at(time, column=1):
            index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
            first, second = rows[index - 1], rows[index]
            return first[column] + (second[column] - first[column]) * (time - first[0]) / (second[0] - first[0])

        rising = [a[0] + (b[0] - a[0]) * (.6 - a[1]) / (b[1] - a[1])
                  for a, b in zip(rows, rows[1:]) if a[0] > 64e-9 and a[1] < .6 <= b[1]]
        if len(rising) < 20:
            raise ValueError('oscillator did not sustain clock edges after reset')
        periods = [b - a for a, b in zip(rising, rising[1:])]
        frequency = 1 / statistics.mean(periods)
        last = [row for row in rows if row[0] > 64e-9]
        def mean(column):
            return sum((b[0] - a[0]) * (a[column] + b[column]) / 2 for a, b in zip(last, last[1:])) / (last[-1][0] - last[0][0])
        supply = mean(3)
        control = mean(2)
        target = supply * (255 * parameters[0] + parameters[3]) / 65536
        shape = [statistics.mean(at(a + (b - a) * phase / 64) for a, b in zip(rising[-17:-1], rising[-16:]))
                 for phase in range(64)]
        high, total = 0., 0.
        for a, b in zip(rows, rows[1:]):
            if a[0] > 64e-9:
                total += b[0] - a[0]
                high += (b[0] - a[0]) * ((a[1] > .6) + (b[1] > .6)) / 2
        power = 1.2 * (1.2 - supply) / 10
        limits = (90e6, 200e6) if name == 'low' else (330e6, 600e6) if name == 'high' else (160e6, 300e6)
        checks = dict(frequency=limits[0] < frequency < limits[1], duty=.2 < high / total < .8,
                      stable_periods=max(periods) / min(periods) < 1.01,
                      low_rail=min(row[1] for row in last) < .15,
                      high_rail=max(row[1] for row in last) > 1.15,
                      loaded_control=abs(control - target) < .002,
                      finite_supply_loading=.00005 < 1.2 - min(row[3] for row in last) < .2,
                      bounded_supply_power=5e-6 < power < .001,
                      physical_reset_clamp=at(12e-9) > 1.0)
        compressed = trace.with_suffix(trace.suffix + '.gz')
        with trace.open('rb') as original, gzip.open(compressed, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(original, output)
        trace.unlink()
        result['cases'][name] = dict(frequency_Hz=frequency, duty=high / total, control_V=control,
            supply_V=supply, supply_power_W=power, phase_shape_V=shape, checks=checks,
            waveform=str(compressed), waveform_sha256=digest(compressed))
        report_path.write_text(json.dumps(result, indent=2) + '\n')
        if not all(checks.values()):
            raise ValueError(name + ' clock contract failed: ' + str(checks))
        print(name, frequency / 1e6, 'MHz', flush=True)
    cases = result['cases']
    frequency = lambda name: cases[name]['frequency_Hz']
    checks = dict(coarse_tuning=frequency('low') < frequency('nominal') < frequency('high'),
                  fine_tuning=frequency('fine_low') < frequency('nominal') < frequency('fine_high'),
                  output_load_changes_frequency=.7 < frequency('loaded') / frequency('nominal') < .98,
                  timestep_convergence=abs(frequency('refined') / frequency('nominal') - 1) < .01)
    result.update(status='pass' if all(checks.values()) else 'fail', checks=checks)
    if any(digest(Path(path)) != value for path, value in provenance.items()):
        result.update(status='fail', error='clock source changed while validation was running')
    report_path.write_text(json.dumps(result, indent=2) + '\n')
    if result['status'] != 'pass':
        raise ValueError('clock tuning/refinement contract failed')


def compare(args):
    left, right = [json.loads((path / 'checks.json').read_text()) for path in (args.work, args.other)]
    if {left.get('flow'), right.get('flow')} != {'cpp', 'veriloga'} or any(r.get('status') != 'pass' for r in (left, right)):
        raise ValueError('both independent oscillator flows must pass first')
    checks = []
    for name in CASES:
        a, b = left['cases'][name], right['cases'][name]
        relative = abs(a['frequency_Hz'] / b['frequency_Hz'] - 1)
        shape = math.sqrt(statistics.mean((x - y) ** 2 for x, y in zip(a['phase_shape_V'], b['phase_shape_V'])))
        if relative > .01 or shape > .03 or abs(a['control_V'] - b['control_V']) > .001:
            raise ValueError('independent clock solvers disagree in ' + name)
        checks.append(dict(case=name, relative_frequency_error=relative, phase_normalized_shape_rms_V=shape))
    (args.work.parent / 'comparison.json').write_text(json.dumps(dict(status='pass', checks=checks), indent=2) + '\n')
    print('PASS same-equation oscillator frequency, control and phase-normalized waveform comparison')


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
