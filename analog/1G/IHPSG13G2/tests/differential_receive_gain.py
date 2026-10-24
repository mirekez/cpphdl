#!/usr/bin/env python3
"""Independent differential-gain purposes across input common mode and solvers."""
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
from compare import checked_rows, errors, read_cpp, read_spice
from programmable_receive_gain import windowed_deck
from receive_gain_process import process_deck
from mos_geometry import size_mos_instances
from time_grid import windowed_transient, check_observed_grid
from trace_states import read_states
from trace_storage import capture_trace

HEADER = ['time_s', 'IN_MINUS', 'OUT', 'BIAS', 'DRIVE', 'FEEDBACK', 'VDD', 'IN_PLUS', 'VREF']
WARMUP = 16e-6


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contract(rows, code):
    checked_rows(rows, len(HEADER), 'differential receive gain')
    times = [row[0] for row in rows]
    plateaus = []
    for symbol in range(32):
        lo, hi = WARMUP + (8 * symbol + 6) * 1e-9, WARMUP + (8 * symbol + 7) * 1e-9
        window = rows[bisect_left(times, lo):bisect_right(times, hi)]
        if len(window) < 10:
            raise ValueError('missing differential gain aperture')
        # Check every solver state against the fitted line below, including
        # repeated times; averaging alone can conceal a bad transient sample.
        plateaus.extend(window)
    x = [row[7] - row[1] for row in plateaus]
    y = [row[2] - row[8] for row in plateaus]
    mx, my = sum(x) / len(x), sum(y) / len(y)
    energy = sum((a - mx) ** 2 for a in x)
    if energy < 1e-4:
        raise ValueError('insufficient differential stimulus')
    gain = sum((a - mx) * (b - my) for a, b in zip(x, y)) / energy
    offset = my - gain * mx
    residual = max(abs(b - offset - gain * a) for a, b in zip(x, y))
    wanted = code / (9 - code)
    checks = dict(gain=.8 * wanted <= gain <= 1.2 * wanted,
        linearity=residual <= .015, offset=abs(offset) <= .015,
        output_headroom=all(.85 <= row[2] <= 2.05 for row in rows),
        drive_headroom=all(1.5 <= row[4] <= 3.25 for row in rows),
        supply=all(3.2 <= row[6] <= 3.35 for row in rows),
        reference=all(1.60 <= row[8] <= 1.70 for row in rows))
    return dict(checks=checks, gain=gain, offset_V=offset, maximum_linearity_residual_V=residual,
        output_range_V=[min(row[2] for row in rows), max(row[2] for row in rows)],
        common_mode_range_V=[min((row[7] + row[1]) / 2 for row in rows),
                             max((row[7] + row[1]) / 2 for row in rows)],
        aperture_states=len(plateaus))


def verify(result):
    failed = [name for name, value in result['checks'].items() if not value]
    if failed:
        raise ValueError('differential gain purpose failed: ' + ', '.join(failed))


def fresh(hashes):
    if any(digest(path) != value for path, value in hashes.items()):
        raise ValueError('differential gain verification inputs changed')


def run(args):
    root = args.work.resolve() / args.flow
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    paths = [SOURCE, SOURCE.with_suffix('.cpp'), args.bench.resolve(),
        SOURCE.parent / 'programmable_receive_gain.py']
    paths += [ANALOG / 'VerilogA' / name for name in
        ('compare.py', 'trace_states.py', 'trace_storage.py', 'time_grid.py')]
    if args.flow != 'cpp':
        paths += [args.ngspice.resolve()]
    if args.flow == 'veriloga':
        paths += [args.openvaf.resolve()]
    if args.flow == 'psp':
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        paths += [SOURCE.parents[1] / name for name in
                  ('receive_gain_process.py', 'mos_geometry.py', 'spice_vectors.py')]
        paths += [models / '.spiceinit']
        paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(path): digest(path) for path in paths})
    report = dict(status='running', flow=args.flow, source_sha256=hashes, work=str(work), cases={},
        corner=args.corner, temperature_C=args.temperature, step_ps=args.step_ps,
        scope='loaded differential PGA with finite external reference and fixed gain controls; no integrated reference buffer, noise, mismatch, extracted layout or full-PHY qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(label, argv, case, env=None):
        with (case / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=case, env=os.environ if env is None else env, stdout=log, stderr=-2, check=True, timeout=1800)

    save()
    try:
        for code in args.codes:
            for common in args.common:
                key = f'{code}_{common:g}'
                case = work / key
                case.mkdir()
                command('bench', [args.bench.resolve(), case, 'run' if args.flow == 'cpp' else 'export', code, common, args.step_ps], case)
                if args.flow == 'cpp':
                    raw = case / 'cpp/trace.csv'
                    header, rows = read_cpp(raw)
                    if header != HEADER:
                        raise ValueError('changed differential gain observations')
                else:
                    deck = (case / 'bench.cir').read_text()
                    environment = dict(os.environ, OMP_NUM_THREADS='1', OMP_THREAD_LIMIT='1')
                    if args.flow == 'veriloga':
                        command('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'], case)
                    else:
                        deck, bindings = process_deck((case / 'design.va').read_text(), deck,
                            (case / 'process_devices.csv').read_text(), models / 'models',
                            args.corner, args.temperature, profile='differential')
                        deck, geometry = size_mos_instances(deck)
                        (case / 'bindings.json').write_text(json.dumps(bindings | dict(geometry=geometry), indent=2) + '\n')
                        environment.update(PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models))
                    deck, grid = windowed_transient(windowed_deck(deck), 1e-9)
                    (case / 'measurement.cir').write_text(deck)
                    with capture_trace(case / 'spice.dat'):
                        command('ngspice', [args.ngspice.resolve(), *(['-n'] if args.flow == 'veriloga' else []), '-b', 'measurement.cir'], case, environment)
                    log = (case / 'ngspice.log').read_text()
                    if re.search(r'\b(error|fatal|failed|aborted)\b|timestep too small', log, re.I):
                        raise ValueError('differential gain simulator failed')
                    raw = case / 'spice.dat.gz'
                    rows = read_states(raw, HEADER)
                    report['last_grid'] = check_observed_grid(rows, grid)
                result = contract(rows, code)
                report['cases'][key] = result | dict(waveform=str(raw), waveform_sha256=digest(raw))
                save()
                verify(result)
                print('PASS', args.flow, key, 'gain', result['gain'], 'offset V', result['offset_V'], flush=True)
        fresh(hashes)
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def compare(args):
    trajectories = []
    for flow in ('cpp', 'veriloga'):
        receipt = json.loads((args.work / flow / 'checks.json').read_text())
        if receipt.get('status') != 'pass' or receipt.get('flow') != flow:
            raise ValueError('both independently passing differential gain flows required')
        fresh(receipt['source_sha256'])
        cases = {}
        for key, previous in receipt['cases'].items():
            raw = Path(previous['waveform'])
            if digest(raw) != previous['waveform_sha256']:
                raise ValueError('changed differential gain trajectory')
            rows = read_cpp(raw)[1] if flow == 'cpp' else read_states(raw, HEADER)
            result = contract(rows, int(key.split('_')[0]))
            verify(result)
            if any(previous[k] != v for k, v in result.items()):
                raise ValueError('changed absolute differential gain results')
            cases[key] = rows
        trajectories.append(cases)
    if set(trajectories[0]) != set(trajectories[1]):
        raise ValueError('different differential gain test matrices')
    results = {key: errors(trajectories[0][key], trajectories[1][key]) for key in trajectories[0]}
    if any(value > .005 for values in results.values() for value in values):
        raise ValueError('C++/Verilog-A differential gain difference exceeds 5 mV')
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass', peak_differences_V=results), indent=2) + '\n')
    print('PASS independent absolute checks and 5 mV waveform comparison')


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
    parser.add_argument('--codes', type=int, nargs='+', choices=(4, 8), default=[4, 8])
    parser.add_argument('--common', type=float, nargs='+', default=[1.1, 1.4, 1.65])
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--step-ps', type=float, default=5)
    args = parser.parse_args()
    if args.flow != 'compare' and args.bench is None:
        parser.error('a compiled differential bench is required')
    if args.flow in ('veriloga', 'psp') and args.ngspice is None:
        parser.error('ngspice is required')
    if args.flow == 'veriloga' and args.openvaf is None:
        parser.error('OpenVAF is required')
    if args.flow == 'psp' and args.pdk is None:
        parser.error('the IHP PDK is required')
    compare(args) if args.flow == 'compare' else run(args)
