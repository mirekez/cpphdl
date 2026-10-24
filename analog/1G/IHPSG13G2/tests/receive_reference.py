#!/usr/bin/env python3
"""Check a physical reference under eight-channel load steps, in two solvers."""
import argparse
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
from receive_gain_process import process_deck
from mos_geometry import size_mos_instances

HEADER = ['time_s', 'OUT', 'VDD', 'TARGET', 'DRIVE', 'LOAD', 'BIAS_N', 'BIAS_P', 'TAIL_BIAS']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contract(rows, ratio=.5, load_profile='wide'):
    if load_profile not in ('wide', 'receiver'):
        raise ValueError('unknown reference load profile')
    low, high = (1.1, 1.9) if load_profile == 'wide' else (.9, 1.5)
    checked_rows(rows, len(HEADER), 'receive reference')
    windows = [[] for _ in range(8)]
    for row in rows:
        phase = (row[0] - 16e-6) / 32e-9
        slot = int(phase)
        if 0 <= slot < 8 and 6e-9 <= (phase - slot) * 32e-9 <= 31e-9:
            windows[slot].append(row)
    if any(len(window) < 100 for window in windows):
        raise ValueError('missing reference load-step aperture')
    settled = max(abs(row[1] - row[2] * ratio) for window in windows for row in window)
    divider_error = max(abs(row[3] - row[2] * ratio) for row in rows)
    excursion = max(abs(row[1] - row[2] * ratio) for row in rows)
    checks = dict(settled_accuracy=settled <= .015, transient_excursion=excursion <= .100,
        divider_ratio=divider_error <= .002, supply=all(3.2 <= row[2] <= 3.35 for row in rows),
        drive_headroom=all(1.5 <= row[4] <= 3.25 for row in rows),
        excitation=abs(min(row[5] for row in rows) - low) <= 1e-6 and
            abs(max(row[5] for row in rows) - high) <= 1e-6)
    result = dict(checks=checks, maximum_settled_error_V=settled, maximum_transient_error_V=excursion,
        maximum_divider_error_V=divider_error, load_profile=load_profile,
        observed_load_range_V=[min(row[5] for row in rows), max(row[5] for row in rows)], loaded_channels=8, total_load_resistance_Ohm=112.5)
    return result


def verify(result):
    failed = [key for key, value in result['checks'].items() if not value]
    if failed:
        raise ValueError('reference purpose failed: ' + ', '.join(failed))


def run(args):
    root = args.work.resolve() / args.flow
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    dependencies = [SOURCE, args.bench.resolve(), SOURCE.parent / 'programmable_receive_gain.py']
    dependencies += [ANALOG / 'VerilogA' / name for name in ('compare.py', 'trace_states.py', 'trace_storage.py', 'time_grid.py')]
    if args.flow != 'cpp':
        dependencies += [args.ngspice.resolve()]
    if args.flow == 'veriloga':
        dependencies += [args.openvaf.resolve(), args.ngspice.resolve()]
    if args.flow == 'psp':
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        dependencies += [SOURCE.parents[1] / name for name in ('receive_gain_process.py', 'mos_geometry.py', 'spice_vectors.py')]
        dependencies += [models / '.spiceinit']
        dependencies += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(path): digest(path) for path in dependencies})
    report = dict(status='running', flow=args.flow, work=str(work), source_sha256=hashes, reference_profile=args.profile, load_profile=args.load_profile, reference_ratio=(.5 if args.profile == 'midrail' else 1000 / 2275), corner=args.corner, temperature_C=args.temperature,
        scope='supply-ratio reference with eight-channel equivalent static load and steps; no integrated PGA charge, integrated receiver/layout/noise qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    environment = dict(os.environ, OMP_THREAD_LIMIT='1', OMP_NUM_THREADS='1')

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, env=environment,
                stdout=log, stderr=-2, check=True, timeout=1800)

    save()
    try:
        command('bench', [args.bench.resolve(), work, 'run' if args.flow == 'cpp' else 'export', args.load_profile])
        if json.loads((work / 'reference_profile.json').read_text()) != dict(profile=args.profile, load_profile=args.load_profile):
            raise ValueError('compiled reference class differs from requested profile')
        if args.flow == 'cpp':
            raw = work / 'cpp/trace.csv'
            header, rows = read_cpp(raw)
            if header != HEADER:
                raise ValueError('changed reference observations')
        else:
            deck = (work / 'bench.cir').read_text()
            if args.flow == 'veriloga':
                command('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
            else:
                deck, bindings = process_deck((work / 'design.va').read_text(), deck,
                    (work / 'process_devices.csv').read_text(), models / 'models', args.corner,
                    args.temperature, profile='reference')
                deck, geometry = size_mos_instances(deck)
                (work / 'bindings.json').write_text(json.dumps(bindings | dict(geometry=geometry), indent=2) + '\n')
                environment.update(PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models))
            deck, grid = windowed_transient(windowed_deck(deck), 1e-9)
            (work / 'measurement.cir').write_text(deck)
            with capture_trace(work / 'spice.dat'):
                command('ngspice', [args.ngspice.resolve(), *(['-n'] if args.flow == 'veriloga' else []), '-b', 'measurement.cir'])
            log = (work / 'ngspice.log').read_text()
            if re.search(r'\b(error|fatal|failed|aborted)\b|timestep too small', log, re.I):
                raise ValueError('reference simulation failed')
            raw = work / 'spice.dat.gz'
            rows = read_states(raw, HEADER)
            report['grid'] = check_observed_grid(rows, grid)
        result = contract(rows, report['reference_ratio'], args.load_profile)
        report.update(result, waveform=str(raw), waveform_sha256=digest(raw))
        verify(result)
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('reference verification inputs changed')
        report['status'] = 'pass'
        print('PASS', args.flow, 'eight-channel reference; maximum settled error V', result['maximum_settled_error_V'])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def compare(args):
    trajectories = []
    receipts = {}
    for flow in ('cpp', 'veriloga'):
        path = args.work / flow / 'checks.json'
        previous = json.loads(path.read_text())
        if previous.get('status') != 'pass' or previous.get('flow') != flow:
            raise ValueError('independently passing reference flows required')
        if any(digest(path) != value for path, value in previous['source_sha256'].items()):
            raise ValueError('stale reference evidence')
        raw = Path(previous['waveform'])
        if digest(raw) != previous['waveform_sha256']:
            raise ValueError('changed reference trajectory')
        rows = read_cpp(raw)[1] if flow == 'cpp' else read_states(raw, HEADER)
        result = contract(rows, previous['reference_ratio'], previous['load_profile'])
        verify(result)
        if any(previous[k] != v for k, v in result.items()):
            raise ValueError('changed reference absolute results')
        trajectories.append(rows)
        receipts[flow] = digest(path)
    profiles = [json.loads((args.work / flow / 'checks.json').read_text()) for flow in ('cpp', 'veriloga')]
    if any(profiles[0][key] != profiles[1][key] for key in ('reference_profile', 'reference_ratio', 'load_profile')):
        raise ValueError('reference comparison requires matching classes and loads')
    differences = errors(*trajectories)
    if any(value > .005 for value in differences):
        raise ValueError('reference C++/Verilog-A difference exceeds 5 mV')
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass',
        peak_differences_V=dict(zip(HEADER[1:], differences)), receipt_sha256=receipts), indent=2) + '\n')
    print('PASS reference independent purposes and waveform comparison')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'psp', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--profile', choices=('midrail', 'adc_range'), default='midrail')
    parser.add_argument('--load-profile', choices=('wide', 'receiver'), default='wide')
    args = parser.parse_args()
    if args.flow != 'compare' and args.bench is None:
        parser.error('a compiled reference bench is required')
    if args.flow == 'veriloga' and (args.openvaf is None or args.ngspice is None):
        parser.error('Verilog-A needs OpenVAF and ngspice')
    if args.flow == 'psp' and (args.ngspice is None or args.pdk is None):
        parser.error('the process flow requires ngspice and the IHP PDK')
    compare(args) if args.flow == 'compare' else run(args)
