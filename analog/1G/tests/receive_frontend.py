#!/usr/bin/env python3
"""Independently check coupled amplifier/buffer/ADC acquisition in both solvers."""
import argparse
import bisect
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
from compare import read_cpp, read_spice
from hardware import configure_cleanup, execute, source_dependencies

CASES = dict(one=(1, .05, 10), four=(4, .05, 10), sixteen=(16, .05, 10),
             loaded=(4, 4, 10), overload=(4, 40, 10), refined=(4, .05, 5))


def digest(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def dc_transfer(plus, minus, supply):
    # Independently solve each degenerated MOS current at a trial tail voltage.
    # Then bisect the shared tail resistor's KCL, including both branches.
    def current(gate, tail):
        overdrive = max(0, gate - tail - .7)
        channel_voltage = 2 * overdrive / (1 + math.sqrt(1 + 4 * .0075 * 50 * overdrive))
        return .0075 * channel_voltage ** 2

    low, high = 0, max(plus, minus)
    for _ in range(64):
        tail = (low + high) / 2
        if tail > 150 * (current(plus, tail) + current(minus, tail)):
            high = tail
        else:
            low = tail
    drain = supply - 500 * current(minus, (low + high) / 2)
    overdrive = max(0, drain - .7)
    term = 2 / 500 / .05
    output = 2 * overdrive ** 2 / (2 * overdrive + term + math.sqrt(term ** 2 + 4 * overdrive * term))
    return drain, output


def contract(rows, count, step):
    header = ['time_s', 'PLUS', 'MINUS', 'DRAIN', 'BUFFER', 'VDD', 'VLOGIC']
    header += ['HELD' + str(i) for i in range(count)]
    # UIC transient output starts at ngspice's first accepted positive step.
    if (not rows or not 0 <= rows[0][0] <= step * 1e-12 or abs(rows[-1][0] - (12 + 8 * count) * 1e-9) > 1e-15
            or any(len(row) != len(header) or not all(math.isfinite(x) for x in row) for row in rows)):
        raise ValueError('incomplete or nonfinite receive-front-end trajectory')
    times = [row[0] for row in rows]
    if any(not 0 < b - a <= step * 1.01e-12 for a, b in zip(times, times[1:])):
        raise ValueError('unordered or undersampled receive-front-end trajectory')

    def at(time, column):
        index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[index - 1], rows[index]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    samples, errors, drain_errors, buffer_errors, held_drifts = [], [], [], [], []
    input_errors = []
    for index in range(2 * count):
        slice_index = index % count
        start = (8 + 4 * index) * 1e-9
        sampled = start + 3.8e-9
        level = .12 * ((index * 3) % 5 - 2)
        p, m, supply = [at(sampled, col) for col in (1, 2, 5)]
        drain, expected = dc_transfer(p, m, supply)
        input_errors += [abs(p - (1.65 + level / 2)), abs(m - (1.65 - level / 2))]
        held = at(sampled, 7 + slice_index)
        errors.append(abs(held - expected))
        drain_errors.append(abs(at(sampled, 3) - drain))
        buffer_errors.append(abs(at(sampled, 4) - expected))
        samples.append(held)
        end = ((8 + 4 * (index + count)) * 1e-9 if index < count else times[-1]) - .1e-9
        held_drifts += [abs(row[7 + slice_index] - held) for row in rows if sampled <= row[0] <= end]
    steady = [row for row in rows if row[0] >= 8e-9]
    checks = dict(input_delivery=max(input_errors) < .002,
                  amplifier_delivery=max(drain_errors) < .02,
                  buffer_delivery=max(buffer_errors) < .02,
                  acquisition=max(errors) < .02,
                  hold=max(held_drifts, default=0) < .02,
                  io_supply=all(3.1 < row[5] < 3.3 for row in steady),
                  core_supply=all(1.05 < row[6] < 1.21 for row in steady),
                  output_headroom=all(.3 < row[4] < row[5] - .3 for row in steady))
    return dict(checks=checks, sample_V=samples, maximum_acquisition_error_V=max(errors),
                maximum_buffer_error_V=max(buffer_errors), maximum_drain_error_V=max(drain_errors),
                maximum_hold_drift_V=max(held_drifts, default=0),
                probes_V=[[at((8 + .1 * i) * 1e-9, column) for column in range(1, len(header))]
                          for i in range(80 * count)])


def verify(result, overloaded):
    for name, passed in result['checks'].items():
        if overloaded and name in ('buffer_delivery', 'acquisition'):
            continue
        if not passed:
            raise ValueError('receive-front-end contract failed: ' + str(result['checks']))
    if overloaded and result['checks']['acquisition']:
        raise ValueError('overload did not expose failed sample delivery')


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(path): digest(path) for path in (SOURCE, ANALOG / 'VerilogA/compare.py')})
    binaries = {str(args.bench.resolve()): digest(args.bench)}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p) for p in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=hashes, binary_sha256=binaries,
                  scope='coupled generic MOS amplifier, shared buffer and ADC sampling; '
                        'host controls and precharge, no SAR decision scheduling, PSP or layout proof')
    try:
        for name, (count, load, step) in CASES.items():
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'bench.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                         str(count), str(load), str(step)], stdout=log, stderr=-2, check=True, timeout=1800)
            header = ['time_s', 'PLUS', 'MINUS', 'DRAIN', 'BUFFER', 'VDD', 'VLOGIC']
            header += ['HELD' + str(i) for i in range(count)]
            if args.flow == 'cpp':
                raw = case / 'cpp/trace.csv'
                actual_header, rows = read_cpp(raw)
                if actual_header != header:
                    raise ValueError('changed receive-front-end observations')
            else:
                for label, command in (('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                                       ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])):
                    with (case / (label + '.log')).open('w') as log:
                        execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=1800)
                raw = case / 'spice.dat'
                rows = read_spice(raw, header)
            result = contract(rows, count, step)
            verify(result, name == 'overload')
            archive = raw.with_suffix(raw.suffix + '.gz')
            original = digest(raw)
            with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as output:
                shutil.copyfileobj(source, output)
            with gzip.open(archive, 'rb') as source:
                if hashlib.file_digest(source, 'sha256').hexdigest() != original:
                    raise ValueError('receive-front-end archive is not lossless')
            raw.unlink()
            result.update(waveform=str(archive), waveform_sha256=digest(archive))
            report['cases'][name] = result
            (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        a, b = [report['cases'][name]['probes_V'] for name in ('four', 'refined')]
        report['maximum_refinement_V'] = max(abs(x - y) for row_a, row_b in zip(a, b) for x, y in zip(row_a, row_b))
        if report['maximum_refinement_V'] > .02:
            raise ValueError('receive-front-end refinement exceeds 20 mV')
        if any(digest(path) != value for path, value in (hashes | binaries).items()):
            raise ValueError('receive-front-end inputs changed during qualification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    work = args.work.resolve()
    (work / 'comparison.json').unlink(missing_ok=True)
    reports = [json.loads((work / flow / 'checks.json').read_text()) for flow in ('cpp', 'veriloga')]
    for flow, report in zip(('cpp', 'veriloga'), reports):
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != set(CASES):
            raise ValueError('both complete receive-front-end flows must pass independently')
        if any(digest(path) != value for path, value in (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale receive-front-end proof')
        for name, result in report['cases'].items():
            verify(result, name == 'overload')
            if digest(result['waveform']) != result['waveform_sha256']:
                raise ValueError('receive-front-end waveform changed')
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('receive-front-end flows tested different sources')
    differences = {}
    for name in CASES:
        a, b = [report['cases'][name] for report in reports]
        wave = max(abs(x - y) for ra, rb in zip(a['probes_V'], b['probes_V']) for x, y in zip(ra, rb))
        sample = max(abs(x - y) for x, y in zip(a['sample_V'], b['sample_V']))
        if wave > .02 or sample > .01:
            raise ValueError('receive-front-end solver disagreement: ' + name)
        differences[name] = dict(maximum_waveform_difference_V=wave, maximum_sample_difference_V=sample)
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
