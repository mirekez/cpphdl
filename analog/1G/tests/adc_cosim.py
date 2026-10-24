#!/usr/bin/env python3
"""Close actual SAR-controller feedback around either electrical solver.

Offline waveform relaxation updates only host-driven control voltages. A result
is accepted only after the complete control trajectory is unchanged by feedback
from that flow's resolved transistor comparator. No ideal input comparison
chooses the trial bits.
"""
import argparse
import bisect
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import read_cpp, read_spice
sys.path.insert(0, str(SOURCE.parent))
from hardware import source_dependencies, configure_cleanup, execute

PERIOD = 4e-9
CLOCKS = 64
SUPPLY = 3.3
PROFILES = dict(pam=(1.15, 1.4, 1.9, 2.15), center=(1.65, 1.649, 1.651, 1.65))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def close_loop(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    provenance = source_dependencies([SOURCE.parent / 'adc_analog.cpp'], ANALOG.parent, args.cxx,
                                    ['-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    provenance[str(SOURCE)] = digest(SOURCE)
    report = dict(status='running', flow=args.flow, iterations=[],
                  profile=args.profile,
                  scope='same-topology generic MOS SAR converter with host clock/gate voltages; not a physical clock tree or IHP ADC',
                  source_sha256=provenance)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    feedback_path = Path('none')
    previous_controls = None
    model_hash = None
    compiled = work / 'design.osdi'
    for iteration in range(20):
        directory = work / f'iteration_{iteration}'
        directory.mkdir(exist_ok=True)
        with (directory / 'bench.log').open('w') as log:
            execute([str(args.bench.resolve()), str(directory), 'run' if args.flow == 'cpp' else 'export',
                     str(feedback_path), args.profile], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
        current_model = digest(directory / 'design.va')
        if model_hash is not None and current_model != model_hash:
            raise ValueError('feedback changed the physical model rather than its host stimulus')
        model_hash = current_model
        if args.flow == 'veriloga':
            if iteration == 0:
                with (directory / 'compile.log').open('w') as log:
                    execute([str(args.openvaf.resolve()), str(directory / 'design.va'), '-o', str(compiled)],
                            stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
            shutil.copyfile(compiled, directory / 'design.osdi')
            with (directory / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'], cwd=directory,
                        stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
            (directory / 'design.osdi').unlink()
        with (directory / 'columns.csv').open() as stream:
            header = next(csv.reader(stream))
        if args.flow == 'cpp':
            actual_header, rows = read_cpp(directory / 'cpp/trace.csv')
            if actual_header != header:
                raise ValueError('native/exported ADC observations differ')
            trace = directory / 'cpp/trace.csv'
        else:
            rows = read_spice(directory / 'spice.dat', header)
            trace = directory / 'spice.dat'
        if (not rows or abs(rows[-1][0] - CLOCKS * PERIOD) > 1e-14 or
                any(value is None or not math.isfinite(value) for row in rows for value in row)):
            raise ValueError('incomplete/nonfinite ADC trajectory')
        times = [row[0] for row in rows]
        if any(b - a > PERIOD / 128 for a, b in zip(times, times[1:])):
            raise ValueError('inadequate ADC trajectory coverage')

        def at(name, time):
            index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
            first, second = rows[index - 1], rows[index]
            column = header.index(name)
            return first[column] + (second[column] - first[column]) * (time - first[0]) / (second[0] - first[0])

        with (directory / 'control.csv').open() as stream:
            controls = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]
        if len(controls) != CLOCKS or [r['clock'] for r in controls] != list(range(CLOCKS)):
            raise ValueError('incomplete SAR controller schedule')
        decisions = [0] * CLOCKS
        for edge in range(1, CLOCKS):
            if controls[edge - 1]['convert']:
                positive, negative = (at(name, edge * PERIOD - .5e-9) for name in ('POSITIVE', 'NEGATIVE'))
                if positive > .7 * SUPPLY and negative < .3 * SUPPLY:
                    decisions[edge] = 1
                elif negative > .7 * SUPPLY and positive < .3 * SUPPLY:
                    decisions[edge] = 0
                else:
                    raise ValueError(f'unresolved physical comparator at clock {edge}: {positive}, {negative}')
        feedback_path = directory / 'feedback.txt'
        feedback_path.write_text(''.join(str(value) + '\n' for value in decisions))
        control_bytes = (directory / 'control.csv').read_bytes()
        stable = previous_controls == control_bytes
        observation = dict(iteration=iteration, controls_unchanged=stable,
                           control_sha256=digest(directory / 'control.csv'),
                           feedback_sha256=digest(feedback_path), waveform_sha256=digest(trace))
        report['iterations'].append(observation)
        if stable:
            results = [row for row in controls if row['valid']]
            codes = [row['data'] for row in results]
            levels = PROFILES[args.profile]
            expected = [math.floor(level * 256 / SUPPLY) for level in levels]
            errors = [abs(a - b) for a, b in zip(codes, expected)]
            held_errors = [abs(at('HELD', (sample * 16 + 11.8) * PERIOD) - level)
                           for sample, level in enumerate(levels)]
            checks = dict(completed_conversions=len(results) == 4,
                          fixed_completion_clocks=[row['clock'] for row in results] == [12, 28, 44, 60],
                          physical_conversion_accuracy=len(errors) == 4 and max(errors) <= 2,
                          source_disturbance_rejected=max(held_errors) < .02,
                          no_busy_at_completion=all(not row['busy'] for row in results))
            report.update(status='pass' if all(checks.values()) else 'fail', checks=checks,
                          codes=codes, expected_quantization=expected, error_counts=errors,
                          held_error_V=held_errors, converged_iteration=iteration,
                          clocks_Hz=250e6, conversion_spacing_s=16 * PERIOD)
        compressed = trace.with_suffix(trace.suffix + '.gz')
        with trace.open('rb') as original, gzip.open(compressed, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(original, output)
        trace.unlink()
        observation['compressed_waveform'] = str(compressed)
        observation['compressed_sha256'] = digest(compressed)
        report_path.write_text(json.dumps(report, indent=2) + '\n')
        if stable:
            break
        previous_controls = control_bytes
    else:
        raise ValueError('ADC feedback did not converge; no closed-loop result')
    if any(digest(Path(path)) != value for path, value in provenance.items()):
        raise ValueError('ADC source changed during co-simulation')
    if report['status'] != 'pass':
        raise ValueError('physical ADC contract failed: ' + str(report['checks']))
    print(json.dumps({key: report[key] for key in ('status', 'flow', 'codes', 'error_counts', 'converged_iteration')}, indent=2))


def compare(args):
    reports = [json.loads((p / 'checks.json').read_text()) for p in (args.work, args.other)]
    if any(r.get('status') != 'pass' for r in reports) or {r['flow'] for r in reports} != {'cpp', 'veriloga'}:
        raise ValueError('both independent electrical flows must pass first')
    if reports[0].get('profile') != reports[1].get('profile'):
        raise ValueError('cannot compare different ADC stimulus profiles')
    error = max(abs(a - b) for a, b in zip(reports[0]['codes'], reports[1]['codes']))
    if error > 1:
        raise ValueError('ADC codes differ by more than one count between solvers')
    print('PASS independently closed native/Verilog-A ADC feedback; maximum code difference:', error)


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
    parser.add_argument('--profile', choices=PROFILES, default='pam')
    args = parser.parse_args()
    try:
        if args.flow == 'compare':
            compare(args)
        else:
            close_loop(args)
    except BaseException as error:
        path = args.work.resolve() / 'checks.json'
        if args.flow != 'compare' and path.is_file():
            report = json.loads(path.read_text())
            report.update(status='fail', error=str(error))
            path.write_text(json.dumps(report, indent=2) + '\n')
        raise
