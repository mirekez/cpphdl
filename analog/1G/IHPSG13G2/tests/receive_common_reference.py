#!/usr/bin/env python3
"""Check a physical shared common-mode sink and every connected RC input."""
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
from programmable_receive_gain import windowed_deck
from receive_common_process import process_deck
from mos_geometry import size_mos_instances

HEADER = ['time_s', 'OUT', 'VDD', 'TARGET', 'DRIVE', 'BIAS_N', 'BIAS_P', 'TAIL_BIAS']
HEADER += [name + str(i) for i in range(8) for name in ('P', 'N', 'PLUS', 'MINUS')]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def rc_step(previous, first, last, dt, tau):
    weight = -math.expm1(-dt / tau)
    return previous * (1 - weight) + first * weight + (last - first) * (1 - tau * weight / dt)


def contract(rows):
    checked_rows(rows, len(HEADER), 'shared common-mode reference')
    times = [row[0] for row in rows]
    if times[0] > 16e-6 + 10e-12 or abs(times[-1] - 16.256e-6) > 1e-14:
        raise ValueError('incomplete common-mode observation interval')
    settled = []
    for index in range(32):
        start, end = 16e-6 + (8 * index + 6) * 1e-9, 16e-6 + (8 * index + 7) * 1e-9
        window = rows[bisect_left(times, start):bisect_right(times, end)]
        if len(window) < 100:
            raise ValueError('missing common-mode settled aperture')
        settled.extend(window)
    maximum_common_error, maximum_diff_error = 0., 0.
    minimum_mean, maximum_mean = math.inf, -math.inf
    injected = []
    tau_mean = 150e-15 / (1 / 500 + 1 / 150)
    tau_difference = 150e-15 / (1 / 500 + 1 / 150 + 2 / 100)
    for lane in range(8):
        p, n, plus, minus = (8 + 4 * lane + i for i in range(4))
        predicted_mean = (rows[0][plus] + rows[0][minus]) / 2
        predicted_diff = rows[0][plus] - rows[0][minus]
        previous = rows[0]
        for row in rows:
            mean, difference = (row[plus] + row[minus]) / 2, row[plus] - row[minus]
            dt = row[0] - previous[0]
            first_mean = (150 * (previous[p] + previous[n]) / 2 + 500 * previous[1]) / 650
            final_mean = (150 * (row[p] + row[n]) / 2 + 500 * row[1]) / 650
            first_diff = 3 / 43 * (previous[p] - previous[n])
            final_diff = 3 / 43 * (row[p] - row[n])
            if dt < 0:
                raise ValueError('unordered common-mode states')
            if dt > 0:
                predicted_mean = rc_step(predicted_mean, first_mean, final_mean, dt, tau_mean)
                predicted_diff = rc_step(predicted_diff, first_diff, final_diff, dt, tau_difference)
            elif max(abs(mean - (previous[plus] + previous[minus]) / 2),
                    abs(difference - previous[plus] + previous[minus])) > 1e-6:
                raise ValueError('instantaneous jump on an explicitly stored input node')
            maximum_common_error = max(maximum_common_error, abs(mean - predicted_mean))
            maximum_diff_error = max(maximum_diff_error, abs(difference - predicted_diff))
            minimum_mean, maximum_mean = min(minimum_mean, mean), max(maximum_mean, mean)
            previous = row
    for row in rows:
        injected.append(sum((row[10 + 4 * lane] + row[11 + 4 * lane] - 2 * row[1]) / 150 for lane in range(8)))
    accuracy = max(abs(row[1] - row[2] * 6 / 11) for row in settled)
    transient = max(abs(row[1] - row[2] * 6 / 11) for row in rows)
    divider = max(abs(row[3] - row[2] * 6 / 11) for row in rows)
    differential_excitation = all(min(row[8 + 4 * lane] - row[9 + 4 * lane] for row in rows) <= -.90 and
        max(row[8 + 4 * lane] - row[9 + 4 * lane] for row in rows) >= .90 for lane in range(8))
    checks = dict(differential_excitation=differential_excitation, settled_reference=accuracy <= .015, transient_reference=transient <= .100,
        divider=divider <= .002, input_common_mode=minimum_mean >= 1.80 and maximum_mean <= 2.00,
        common_rc_equation=maximum_common_error <= .0005,
        differential_rc_equation=maximum_diff_error <= .0005,
        sink_loading=min(injected) >= .006 and max(injected) <= .020,
        supply=all(3.2 <= row[2] <= 3.35 for row in rows),
        drive=all(.8 <= row[4] <= 2.9 for row in rows),
        stimulus=min((row[8] + row[9]) / 2 for row in rows) <= 2.10 and
            max((row[8] + row[9]) / 2 for row in rows) >= 2.46)
    return dict(checks=checks, maximum_settled_error_V=accuracy, maximum_transient_error_V=transient,
        maximum_divider_error_V=divider, input_common_range_V=[minimum_mean, maximum_mean],
        maximum_common_rc_error_V=maximum_common_error, maximum_differential_rc_error_V=maximum_diff_error,
        injected_current_range_A=[min(injected), max(injected)], lanes=8,
        rc_oracle='exact first-order response to piecewise-linear achieved terminal voltages; all solver states, no aperture averaging')


def verify(result):
    failed = [k for k, value in result['checks'].items() if not value]
    if failed:
        raise ValueError('shared common-mode purpose failed: ' + ', '.join(failed))


def run(args):
    root = args.work.resolve() / args.flow
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    paths = [SOURCE, args.bench.resolve(), SOURCE.with_name('programmable_receive_gain.py'),
        SOURCE.parents[1] / 'receive_common_process.py']
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
        scope='shared physical shunt reference with eight loaded RC input networks, finite line/supply impedances and coincident common-mode steps; no amplifier/ADC/hybrid/driver/pad/ESD/full-PHY qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    environment = dict(os.environ, OMP_THREAD_LIMIT='1', OMP_NUM_THREADS='1')

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, env=environment, stdout=log, stderr=-2, check=True, timeout=1800)

    save()
    try:
        command('bench', [args.bench.resolve(), work, 'run' if args.flow == 'cpp' else 'export'])
        if args.flow == 'cpp':
            raw = work / 'cpp/trace.csv'
            rows = read_states(raw, HEADER, csv_format=True)
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
                raise ValueError('shared common-mode simulation failed')
            raw = work / 'spice.dat.gz'
            rows = read_states(raw, HEADER)
            report['observed_grid'] = check_observed_grid(rows, grid)
        result = contract(rows)
        report.update(result, waveform=str(raw), waveform_sha256=digest(raw))
        verify(result)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('shared common-mode verification inputs changed')
        report['status'] = 'pass'
        print('PASS', args.flow, 'eight-lane common-mode sink; settled error V', result['maximum_settled_error_V'], flush=True)
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
            raise ValueError('independently passing common-mode flows required')
        if any(digest(p) != h for p, h in previous['source_sha256'].items()):
            raise ValueError('stale shared common-mode evidence')
        raw = Path(previous['waveform'])
        if digest(raw) != previous['waveform_sha256']:
            raise ValueError('changed common-mode waveform')
        rows = read_states(raw, HEADER, csv_format=flow == 'cpp')
        result = contract(rows)
        verify(result)
        if any(previous.get(k) != v for k, v in result.items()):
            raise ValueError('changed absolute shared common-mode results')
        trajectories.append(rows)
        receipts[flow] = digest(path)
    differences = errors(*trajectories)
    if max(differences) > .005:
        raise ValueError('common-mode generic waveforms differ by more than 5 mV')
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass',
        maximum_differences_V=dict(zip(HEADER[1:], differences)), receipt_sha256=receipts), indent=2) + '\n')
    print('PASS independently checked shared common-mode generic comparison')


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
    args = parser.parse_args()
    if args.flow != 'compare' and args.bench is None:
        parser.error('a compiled bench is required')
    if args.flow == 'veriloga' and not all((args.ngspice, args.openvaf)):
        parser.error('Verilog-A requires ngspice and OpenVAF')
    if args.flow == 'psp' and not all((args.ngspice, args.pdk)):
        parser.error('PSP requires ngspice and the IHP PDK')
    compare(args) if args.flow == 'compare' else run(args)
