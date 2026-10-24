#!/usr/bin/env python3
"""Check the coupled passive MDI, differential stage, gain and physical reference."""
import argparse
from bisect import bisect_left, bisect_right
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / 'VerilogA'), str(ANALOG / '1G/tests'), str(SOURCE.parents[1])]
from hardware import configure_cleanup, execute, source_dependencies
from compare import read_cpp, errors
from trace_states import read_states
from trace_storage import capture_trace
from time_grid import windowed_transient, check_observed_grid
from programmable_receive_gain import windowed_deck
from differential_preamplifier_process import process_deck
from mos_geometry import size_mos_instances
from differential_receive_gain import HEADER as GAIN_HEADER, contract as gain_contract, verify as verify_gain

HEADER = GAIN_HEADER + ['P', 'N', 'SENSE_PLUS', 'SENSE_MINUS']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contract(rows, code):
    gain = gain_contract([row[:len(GAIN_HEADER)] for row in rows], code)
    verify_gain(gain)
    times = [row[0] for row in rows]
    plateaus = []
    for symbol in range(32):
        lo, hi = 16e-6 + (8 * symbol + 6) * 1e-9, 16e-6 + (8 * symbol + 7) * 1e-9
        plateaus.extend(rows[bisect_left(times, lo):bisect_right(times, hi)])
    x = [row[9] - row[10] for row in plateaus]
    y = [row[2] - row[8] for row in plateaus]
    mx, my = sum(x) / len(x), sum(y) / len(y)
    energy = sum((a - mx) ** 2 for a in x)
    if energy < 1e-4:
        raise ValueError('missing MDI differential excitation')
    transfer = sum((a - mx) * (b - my) for a, b in zip(x, y)) / energy
    # The network is a passive resistor bridge at settled DC. Its achieved
    # line voltage includes loading through the two finite source impedances.
    attenuation_error = max(abs((row[11] - row[12]) - .02125 * (row[9] - row[10])) for row in plateaus)
    sense_cm = [(row[11] + row[12]) / 2 for row in rows]
    checks = dict(gain_purpose=all(gain['checks'].values()),
        mdi_attenuation=attenuation_error <= .0005,
        input_common_mode=min(sense_cm) >= 1.85 and max(sense_cm) <= 1.95,
        positive_transfer=.02 <= transfer / (code / (9 - code)) <= .08,
        usable_excitation=max(row[2] for row in rows) - min(row[2] for row in rows) >= .03)
    if not all(checks.values()):
        raise ValueError('coupled preamplifier purpose failed: ' + ', '.join(k for k, v in checks.items() if not v))
    return dict(checks=checks, gain_stage=gain, line_to_output_gain=transfer,
        maximum_attenuation_error_V=attenuation_error, sense_common_mode_range_V=[min(sense_cm), max(sense_cm)])


def run(args):
    root = args.work.resolve() / args.flow
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    paths = [SOURCE, args.bench.resolve(), SOURCE.parent / 'differential_receive_gain.py',
             SOURCE.parent / 'programmable_receive_gain.py']
    paths += [ANALOG / 'VerilogA' / name for name in ('compare.py', 'trace_states.py', 'trace_storage.py', 'time_grid.py')]
    if args.flow != 'cpp':
        paths += [args.ngspice.resolve()]
    if args.flow == 'veriloga':
        paths += [args.openvaf.resolve(), args.ngspice.resolve()]
    if args.flow == 'psp':
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        paths += [SOURCE.parents[1] / name for name in ('differential_preamplifier_process.py', 'mos_geometry.py', 'spice_vectors.py')]
        paths += [models / '.spiceinit']
        paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(path): digest(path) for path in paths})
    report = dict(status='running', flow=args.flow, work=str(work), source_sha256=hashes, cases={}, corner=args.corner, temperature_C=args.temperature,
        scope='coupled MDI resistor network, one differential input stage, two buffers, PGA and physical supply-ratio reference; finite linear source impedances, no physical TX/ADC/noise/extracted layout qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    environment = dict(os.environ, OMP_THREAD_LIMIT='1', OMP_NUM_THREADS='1')

    def command(case, label, argv):
        with (case / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=case, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=1800)

    save()
    try:
        for code in (4, 8):
            case = work / str(code)
            case.mkdir()
            command(case, 'bench', [args.bench.resolve(), case, 'run' if args.flow == 'cpp' else 'export', code, 5])
            if args.flow == 'cpp':
                raw = case / 'cpp/trace.csv'
                header, rows = read_cpp(raw)
                if header != HEADER:
                    raise ValueError('changed coupled receiver observations')
            else:
                deck = (case / 'bench.cir').read_text()
                if args.flow == 'veriloga':
                    command(case, 'compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
                else:
                    deck, bindings = process_deck((case / 'design.va').read_text(), deck,
                        models / 'models', args.corner, args.temperature)
                    deck, geometry = size_mos_instances(deck)
                    (case / 'bindings.json').write_text(json.dumps(bindings | dict(geometry=geometry), indent=2) + '\n')
                    environment.update(PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models))
                deck, grid = windowed_transient(windowed_deck(deck), 1e-9)
                (case / 'measurement.cir').write_text(deck)
                with capture_trace(case / 'spice.dat'):
                    command(case, 'ngspice', [args.ngspice.resolve(), *(['-n'] if args.flow == 'veriloga' else []), '-b', 'measurement.cir'])
                log = (case / 'ngspice.log').read_text()
                if re.search(r'\b(error|fatal|failed|aborted)\b|timestep too small', log, re.I):
                    raise ValueError('coupled receiver simulation failed')
                raw = case / 'spice.dat.gz'
                rows = read_states(raw, HEADER)
                report['last_grid'] = check_observed_grid(rows, grid)
            result = contract(rows, code)
            report['cases'][str(code)] = result | dict(waveform=str(raw), waveform_sha256=digest(raw))
            save()
            print('PASS', args.flow, 'code', code, 'line to output gain', result['line_to_output_gain'], flush=True)
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('coupled receiver inputs changed during verification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def compare(args):
    trajectories = []
    receipt_hashes = {}
    for flow in ('cpp', 'veriloga'):
        path = args.work / flow / 'checks.json'
        report = json.loads(path.read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != {'4', '8'}:
            raise ValueError('independently passing coupled receiver flows required')
        if any(digest(path) != value for path, value in report['source_sha256'].items()):
            raise ValueError('stale coupled receiver evidence')
        cases = {}
        for key, previous in report['cases'].items():
            raw = Path(previous['waveform'])
            if digest(raw) != previous['waveform_sha256']:
                raise ValueError('changed coupled receiver trajectory')
            rows = read_cpp(raw)[1] if flow == 'cpp' else read_states(raw, HEADER)
            result = contract(rows, int(key))
            if any(previous[k] != v for k, v in result.items()):
                raise ValueError('changed coupled receiver absolute results')
            cases[key] = rows
        trajectories.append(cases)
        receipt_hashes[flow] = digest(path)
    result = {key: errors(trajectories[0][key], trajectories[1][key]) for key in ('4', '8')}
    if any(value > .005 for values in result.values() for value in values):
        raise ValueError('coupled receiver waveform difference exceeds 5 mV')
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass', peak_differences_V=result,
        receipt_sha256=receipt_hashes), indent=2) + '\n')
    print('PASS coupled receiver independent purposes and waveform comparison')


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
    args = parser.parse_args()
    if args.flow != 'compare' and args.bench is None:
        parser.error('a compiled preamplifier bench is required')
    if args.flow == 'veriloga' and (args.openvaf is None or args.ngspice is None):
        parser.error('Verilog-A requires OpenVAF and ngspice')
    if args.flow == 'psp' and (args.ngspice is None or args.pdk is None):
        parser.error('PSP requires ngspice and the IHP PDK')
    compare(args) if args.flow == 'compare' else run(args)
