#!/usr/bin/env python3
"""Close SAR decisions around the loaded differential receive hierarchy."""
import argparse
import bisect
import csv
import json
import math
from pathlib import Path

from adc_array import digest, live_flow
from receive_frontend import dc_transfer
from hardware import configure_cleanup, execute, source_dependencies

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def check(work, flow, count, bursts, transfer=dc_transfer):
    from compare import read_cpp, read_spice
    if flow == 'cpp':
        header, rows = read_cpp(work / 'cpp/trace.csv')
        trace = work / 'cpp/trace.csv'
    else:
        header = next(csv.reader((work / 'columns.csv').open()))
        trace = work / 'spice.dat'
        rows = read_spice(trace, header)
    conversions, period = count * bursts, 4e-9
    clocks = conversions + 24
    if (not rows or not 0 <= rows[0][0] <= period / 1024 or abs(rows[-1][0] - clocks * period) > 1e-14
            or any(not all(math.isfinite(x) for x in row) for row in rows)):
        raise ValueError('incomplete/nonfinite receive conversion')
    times = [row[0] for row in rows]
    if any(not 0 < b - a <= period / 128 for a, b in zip(times, times[1:])):
        raise ValueError('unordered or undersampled receive conversion')

    def at(name, time):
        col = header.index(name)
        index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[index - 1], rows[index]
        return a[col] + (b[col] - a[col]) * (time - a[0]) / (b[0] - a[0])

    with (work / 'controls.csv').open() as stream:
        controls = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(stream)]
    if [row['clock'] for row in controls] != list(range(-1, clocks)) or any(row['fault'] for row in controls):
        raise ValueError('receive controller fault or incomplete trajectory')
    raw = [row for row in controls if row['valid']]
    normalized = [row for row in controls if row['normalized_valid']]
    for records, delay, identity in ((raw, 13, 'slice'), (normalized, 21, 'normalized_slice')):
        if ([row['clock'] for row in records] != list(range(delay, delay + conversions)) or
                [row[identity] for row in records] != [i % count for i in range(conversions)]):
            raise ValueError('receive conversion cadence or slice identity differs')
    targets, errors, hold_errors, input_errors = [], [], [], []
    frontend_errors, acquisition_errors, hold_drifts = [], [], []
    for index, result in enumerate(raw):
        # Acquisition closes on n+3. Its source transition has already passed
        # through the actual finite-impedance amplifier and common buffer.
        level = .12 * (((index + 3) * 3) % 5 - 2)
        start, end = (index + 3.5) * period, (index + 12.5) * period
        plus, minus, supply = [at(name, start + .5e-9) for name in ('PLUS', 'MINUS', 'VDD')]
        input_errors += [abs(plus - (1.65 + level / 2)), abs(minus - (1.65 - level / 2))]
        _, target = transfer(plus, minus, supply)
        targets.append(target)
        aperture = at('INPUT', start)
        held = at('HELD' + str(index % count), start + 1e-9)
        frontend_errors.append(abs(aperture - target))
        acquisition_errors.append(abs(held - aperture))
        expected = math.floor(target * 256 / supply)
        errors.append(abs(result['data'] - expected))
        hold_errors += [abs(at('HELD' + str(index % count), start + 1e-9 +
                                  sample * (end - start - 2e-9) / 32) - target) for sample in range(33)]
        hold_drifts += [abs(at('HELD' + str(index % count), start + 1e-9 +
                                  sample * (end - start - 2e-9) / 32) - held) for sample in range(33)]
    expected_normalized = []
    for result in raw:
        value = (result['data'] - 128) * 424 / 256
        expected_normalized.append(max(0, min(255, 128 +
            int(math.copysign(math.floor(abs(value) + .5), value)))))
    delivered = True
    for previous in controls[:-1]:
        time = (previous['clock'] + 1.5) * period
        for slice_index in range(count):
            if previous['convert'] & (1 << slice_index):
                value = at('CORE_DECISION' + str(slice_index), time)
                delivered &= value < .36 or value > .84
    steady = [row for row in rows if row[0] > 2e-9]
    supplies = [row[header.index('VDD')] for row in steady]
    core = [row[header.index('VLOGIC')] for row in steady]
    checks = dict(input_delivery=max(input_errors) < .002,
                  conversion_accuracy=max(errors) <= 2,
                  frontend_at_aperture=max(frontend_errors) < .02,
                  sampler_acquisition=max(acquisition_errors) < .02,
                  hold=max(hold_drifts) < .02,
                  io_supply=3 < min(supplies) < 3.3 and max(supplies) < 3.35,
                  core_supply=min(core) > 1.08 and max(core) < 1.35,
                  core_decisions_delivered=bool(delivered),
                  normalization=[row['normalized'] for row in normalized] == expected_normalized,
                  unclipped=not any(row['clipped'] for row in normalized))
    result = dict(checks=checks, codes=[row['data'] for row in raw],
                  normalized_codes=[row['normalized'] for row in normalized],
                  expected_levels_V=targets, code_error_counts=errors,
                  maximum_hold_error_V=max(hold_errors), minimum_supply_V=min(supplies),
                  maximum_frontend_aperture_error_V=max(frontend_errors),
                  maximum_sampler_acquisition_error_V=max(acquisition_errors),
                  maximum_hold_drift_V=max(hold_drifts),
                  minimum_core_supply_V=min(core), waveform_sha256=digest(trace),
                  controls_sha256=digest(work / 'controls.csv'))
    if not all(checks.values()):
        (work / 'failed_contract.json').write_text(json.dumps(result, indent=2) + '\n')
        raise ValueError('receive conversion contract failed: ' + str(checks))
    return result


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in (SOURCE, SOURCE.with_name('receive_frontend.py'), SOURCE.with_name('adc_array.py'),
                 ANALOG / 'VerilogA/live.py', ANALOG / 'VerilogA/compare.py'):
        hashes[str(path)] = digest(path)
    binaries = {str(args.bench.resolve()): digest(args.bench)}
    if args.flow == 'veriloga':
        binaries.update({str(path.resolve()): digest(path) for path in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, slices=args.slices, bursts=args.bursts,
                  source_sha256=hashes, binary_sha256=binaries,
                  scope='closed generic MOS amplifier/buffer/ADC/core-controller conversion; '
                        'host timing and gate buffers, fixed test calibration, no PSP/layout/full-PHY qualification')
    try:
        if args.flow == 'cpp':
            with (work / 'native.log').open('w') as log:
                execute([str(args.bench.resolve()), str(work), 'run', str(args.slices), '.25', str(args.bursts)],
                        stdout=log, stderr=-2, check=True, timeout=10800)
        else:
            live_flow(args, work)
        report.update(check(work, args.flow, args.slices, args.bursts))
        if any(digest(Path(path)) != value for path, value in (hashes | binaries).items()):
            raise ValueError('receive conversion inputs changed during qualification')
        report['status'] = 'pass'
        print('PASS', args.flow, args.slices, 'slices:', report['codes'])
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
        if (report.get('status') != 'pass' or report.get('flow') != flow or
                report.get('slices') != args.slices or report.get('bursts') != args.bursts):
            raise ValueError('both receive conversion flows must pass independently')
        if any(digest(Path(path)) != value for path, value in (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale receive conversion proof')
        current = check(work / flow, flow, args.slices, args.bursts)
        if any(report[key] != value for key, value in current.items()):
            raise ValueError('receive conversion evidence changed')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('receive conversion flows tested different sources')
    differences = {}
    for key, tolerance in (('codes', 1), ('normalized_codes', 2)):
        a, b = [report[key] for report in reports]
        if len(a) != args.slices * args.bursts or len(b) != len(a):
            raise ValueError('receive conversion code coverage differs')
        delta = [abs(x - y) for x, y in zip(a, b)]
        if max(delta) > tolerance:
            raise ValueError('receive conversion solver disagreement: ' + key)
        differences[key] = delta
    (work / 'comparison.json').write_text(json.dumps(dict(status='pass', code_differences=differences,
        flow_report_sha256={flow: digest(work / flow / 'checks.json') for flow in ('cpp', 'veriloga')}), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--fixture-source', type=Path, default=SOURCE.with_suffix('.cpp'))
    parser.add_argument('--slices', type=int, choices=(1, 4, 16), required=True)
    parser.add_argument('--bursts', type=int, choices=(1, 2), default=1)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    if args.bursts > 1 and args.slices != 16:
        parser.error('repeated operation requires sixteen slices')
    compare(args) if args.flow == 'compare' else run(args)
