#!/usr/bin/env python3
"""Check core-voltage trial controls driving a real translated SAR DAC."""
import argparse
from bisect import bisect_left, bisect_right
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / 'VerilogA'), str(ANALOG / '1G/tests'), str(SOURCE.parents[1])]
from hardware import configure_cleanup, execute, source_dependencies
from compare import checked_rows, read_cpp, errors
from trace_states import read_states
from trace_storage import capture_trace
from time_grid import windowed_transient, check_observed_grid
from spice_vectors import keep_observed_voltages
from core_receive_dac_process import process_deck
from mos_geometry import size_mos_instances

HEADER = ['time_s', 'OUT', 'VDD', 'VLOGIC', 'BIAS']
HEADER += [name + str(i) for i in range(8) for name in ('TRIAL', 'BIT_N', 'COMPLEMENT', 'LOW', 'LOW_N')]
CODES = (0, 255, 0, 128, 64, 32, 16, 8, 4, 2, 1, 85, 170, 127, 128, 255)


def windowed_deck(deck):
    lines = deck.splitlines()
    indices = [i for i, line in enumerate(lines) if line.startswith('tran ')]
    if len(indices) != 1:
        raise ValueError('expected one receive DAC transient')
    i = indices[0]
    fields = lines[i].split()
    if len(fields) != 6 or fields[3] != '0' or fields[-1] != 'uic':
        raise ValueError('changed receive DAC transient interface')
    fields[3] = '8e-6'
    lines[i] = ' '.join(fields)
    return keep_observed_voltages('\n'.join(lines) + '\n')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contract(rows):
    checked_rows(rows, len(HEADER), 'core-controlled receive DAC')
    times = [r[0] for r in rows]
    if times[0] > 8e-6 + 20e-12 or abs(times[-1] - 8.064e-6) > 1e-14:
        raise ValueError('incomplete receive DAC measurement window')
    aperture_errors, input_errors, translated_errors = [], [], []
    for index, code in enumerate(CODES):
        start = 8e-6 + index * 4e-9
        window = rows[bisect_left(times, start + 1.5e-9):bisect_right(times, start + 2e-9)]
        if len(window) < 20:
            raise ValueError('missing receive DAC decision aperture')
        aperture_errors.append(max(abs(r[1] * 256 / r[2] - code) for r in window))
        for bit in range(8):
            value = (code >> bit) & 1
            input_errors.append(max(abs(r[5 + 5 * bit] - 1.2 * value) for r in window))
            translated_errors.append(max(abs(r[6 + 5 * bit] / r[2] - (1 - value)) for r in window))
    exposure = 0.
    for row in rows:
        for bit in range(8):
            a, _, complement, low, low_n = row[5 + 5 * bit:10 + 5 * bit]
            exposure = max(exposure, abs(a), abs(complement), abs(low), abs(low_n),
                abs(a - complement), abs(a - row[3]), abs(complement - row[3]),
                abs(a - low_n), abs(complement - low))
    checks = dict(settling=max(aperture_errors) <= 2,
        core_commands=max(input_errors) <= .02, translated_rails=max(translated_errors) <= .1,
        io_supply=all(3 <= r[2] <= 3.35 for r in rows),
        core_supply=all(1.08 <= r[3] <= 1.35 for r in rows),
        bias=all(.8 <= r[4] <= 2.9 for r in rows),
        core_device_exposure=exposure <= 1.65,
        output_range=all(-.1 <= r[1] <= r[2] + .1 for r in rows),
        excitation=max(r[1] for r in rows) - min(r[1] for r in rows) >= 3)
    return dict(checks=checks, maximum_aperture_error_counts=max(aperture_errors),
        aperture_errors_counts=aperture_errors, maximum_core_input_error_V=max(input_errors),
        maximum_translated_rail_error_fraction=max(translated_errors),
        maximum_core_device_exposure_V=exposure,
        bias_range_V=[min(r[4] for r in rows), max(r[4] for r in rows)],
        codes=list(CODES), aperture_s=[1.5e-9, 2e-9],
        scope='two-count DAC settling budget before the opposite 250 MHz edge; comparator settling and complete closed conversion still require connected tests')


def verify(result):
    failed = [name for name, passed in result['checks'].items() if not passed]
    if failed:
        raise ValueError('core receive DAC purpose failed: ' + ', '.join(failed))


def run(args):
    root = args.work.resolve() / args.flow
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    paths = [SOURCE, args.bench.resolve(), SOURCE.parents[1] / 'core_receive_dac_process.py',
        SOURCE.parents[1] / 'core_driver_process.py', SOURCE.parents[1] / 'spice_vectors.py']
    paths += [ANALOG / 'VerilogA' / name for name in ('compare.py', 'trace_states.py', 'trace_storage.py', 'time_grid.py')]
    if args.flow != 'cpp':
        paths += [args.ngspice.resolve()]
    if args.flow == 'veriloga':
        paths += [args.openvaf.resolve()]
    if args.flow == 'psp':
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        paths += [SOURCE.parents[1] / 'mos_geometry.py', models / '.spiceinit']
        paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(p): digest(p) for p in paths})
    report = dict(status='running', source_sha256=hashes, flow=args.flow, work=str(work),
        corner=args.corner, temperature_C=args.temperature,
        scope='eight translated DAC controls, shared physical bias, actual R/2R drivers and finite supplies; no complete comparator, SAR bank or PHY qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    environment = dict(os.environ, OMP_THREAD_LIMIT='1', OMP_NUM_THREADS='1')

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, env=environment, stdout=log, stderr=-2, check=True, timeout=args.simulation_timeout)

    save()
    try:
        command('bench', [args.bench.resolve(), work, 'run' if args.flow == 'cpp' else 'export'])
        if args.flow == 'cpp':
            raw = work / 'cpp/trace.csv'
            rows = read_states(raw, HEADER, csv_format=True)
            times = [row[0] for row in rows]
            report['observed_grid'] = dict(complete=check_observed_grid(rows,
                dict(start_s=8e-6, stop_s=8.064e-6, measurement_maximum_step_s=2e-12)), switching=[])
            for index in range(len(CODES)):
                start = 8e-6 + index * 4e-9
                stop = start + 1.5e-9
                selected = rows[bisect_left(times, start):bisect_right(times, stop + 16 * math.ulp(stop))]
                report['observed_grid']['switching'].append(check_observed_grid(selected,
                    dict(start_s=start, stop_s=stop, measurement_maximum_step_s=.05e-12)))
        else:
            deck = (work / 'bench.cir').read_text()
            if args.flow == 'veriloga':
                command('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
            else:
                deck, inventory = process_deck((work / 'design.va').read_text(), deck, models / 'models', args.corner, args.temperature)
                deck, geometry = size_mos_instances(deck)
                (work / 'bindings.json').write_text(json.dumps(inventory | dict(geometry=geometry), indent=2) + '\n')
                environment.update(PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models))
            deck, grid = windowed_transient(windowed_deck(deck), 1e-9)
            (work / 'measurement.cir').write_text(deck)
            with capture_trace(work / 'spice.dat'):
                command('ngspice', [args.ngspice.resolve(), *(['-n'] if args.flow == 'veriloga' else []), '-b', 'measurement.cir'])
            log = (work / 'ngspice.log').read_text()
            if re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small', log, re.I):
                raise ValueError('core receive DAC simulation failed')
            raw = work / 'spice.dat.gz'
            rows = read_states(raw, HEADER)
            report['observed_grid'] = check_observed_grid(rows, grid)
        result = contract(rows)
        report.update(result, waveform=str(raw), waveform_sha256=digest(raw))
        verify(result)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('core receive DAC verification inputs changed')
        report['status'] = 'pass'
        print('PASS', args.flow, 'core receive DAC; aperture error counts', result['maximum_aperture_error_counts'], flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def compare(args):
    trajectories, receipts = [], {}
    for flow in ('cpp', 'veriloga'):
        path = args.work / flow / 'checks.json'
        previous = json.loads(path.read_text())
        if previous.get('status') != 'pass' or previous.get('flow') != flow:
            raise ValueError('independently passing receive DAC flows required')
        if any(digest(p) != h for p, h in previous['source_sha256'].items()):
            raise ValueError('stale core receive DAC evidence')
        raw = Path(previous['waveform'])
        if digest(raw) != previous['waveform_sha256']:
            raise ValueError('changed receive DAC waveform')
        rows = read_states(raw, HEADER, csv_format=flow == 'cpp')
        result = contract(rows)
        verify(result)
        if any(previous.get(k) != v for k, v in result.items()):
            raise ValueError('changed absolute core receive DAC results')
        trajectories.append(rows)
        receipts[flow] = digest(path)
    differences = errors(*trajectories)
    passed = max(differences) <= .005
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass' if passed else 'fail',
        tolerance_V=.005, maximum_differences_V=dict(zip(HEADER[1:], differences)),
        receipt_sha256=receipts), indent=2) + '\n')
    if not passed:
        raise ValueError('receive DAC generic waveforms differ by more than 5 mV')
    print('PASS independently checked core receive DAC generic comparison')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'psp', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--simulation-timeout', type=float, default=3600)
    args = parser.parse_args()
    if not math.isfinite(args.simulation_timeout) or args.simulation_timeout <= 0:
        parser.error('simulation timeout must be finite and positive')
    if args.flow != 'compare' and args.bench is None:
        parser.error('a compiled bench is required')
    if args.flow == 'veriloga' and not all((args.ngspice, args.openvaf)):
        parser.error('Verilog-A requires ngspice and OpenVAF')
    if args.flow == 'psp' and not all((args.ngspice, args.pdk)):
        parser.error('PSP requires ngspice and the IHP PDK')
    compare(args) if args.flow == 'compare' else run(args)
