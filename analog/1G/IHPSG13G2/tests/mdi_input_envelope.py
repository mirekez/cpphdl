#!/usr/bin/env python3
"""Check the voltages actually delivered to the PGA and its shared reference."""
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
from trace_states import read_states
from trace_storage import capture_trace
from time_grid import windowed_transient, check_observed_grid
from programmable_receive_gain import windowed_deck
from differential_receive_gain import contract as gain_contract, verify, HEADER as GAIN_HEADER
from differential_preamplifier import HEADER
from differential_preamplifier_process import process_deck
from mos_geometry import size_mos_instances


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contract(rows, code):
    gain = gain_contract([row[:9] for row in rows], code)
    verify(gain)
    means = [(row[1] + row[7]) / 2 for row in rows]
    terminals = [row[column] for row in rows for column in (1, 7)]
    common_errors = [abs((row[11] + row[12]) / 2 -
        (1000 * row[6] / 2 + 665 * (row[9] + row[10]) / 2) / 1665) for row in rows]
    times = [row[0] for row in rows]
    plateau_error = []
    for index in range(32):
        lo, hi = 16e-6 + (8 * index + 6) * 1e-9, 16e-6 + (8 * index + 7) * 1e-9
        plateau_error += [abs((row[11] - row[12]) - .02125 * (row[9] - row[10]))
            for row in rows[bisect_left(times, lo):bisect_right(times, hi)]]
    if len(plateau_error) < 320:
        raise ValueError('missing input-network settled apertures')
    checks = dict(gain_purpose=all(gain['checks'].values()),
        pga_mean=min(means) >= 1.1 and max(means) <= 1.45,
        reference_load_range=min(terminals) >= .9 and max(terminals) <= 1.5,
        common_mode_equation=max(common_errors) <= .002,
        differential_equation=max(plateau_error) <= .0005)
    result = dict(checks=checks, pga_mean_range_V=[min(means), max(means)],
        pga_terminal_range_V=[min(terminals), max(terminals)],
        maximum_common_mode_error_V=max(common_errors), maximum_attenuation_error_V=max(plateau_error),
        gain_stage=gain)
    return result


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, args.bench.resolve(), args.ngspice.resolve(),
        SOURCE.with_name('differential_receive_gain.py'), SOURCE.with_name('differential_preamplifier.py'),
        SOURCE.with_name('programmable_receive_gain.py')]
    paths += [SOURCE.parents[1] / name for name in ('differential_preamplifier_process.py', 'mos_geometry.py', 'spice_vectors.py')]
    paths += [ANALOG / 'VerilogA' / name for name in ('compare.py', 'trace_states.py', 'trace_storage.py', 'time_grid.py')]
    paths += [models / '.spiceinit'] + [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([SOURCE.with_name('differential_preamplifier.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(p): digest(p) for p in paths})
    report = dict(status='running', source_sha256=hashes, work=str(work), cases={}, corner=args.corner,
        temperature_C=args.temperature, scope='loaded MDI common-mode/PGA/reference input envelope; nominal 3.3 V supply and passives, no pad/ESD/physical TX/clock/full PHY qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(case, label, argv):
        with (case / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=case, env=dict(os.environ, OMP_THREAD_LIMIT='1', OMP_NUM_THREADS='1',
                PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models)),
                stdout=log, stderr=-2, check=True, timeout=1800)

    save()
    try:
        for common in (2.1, 2.5):
            for code in (4, 8):
                key = f'{common:g}_{code}'
                case = work / key
                case.mkdir()
                command(case, 'export', [args.bench.resolve(), case, 'export', code, 5, common])
                deck, bindings = process_deck((case / 'design.va').read_text(), (case / 'bench.cir').read_text(),
                    models / 'models', args.corner, args.temperature)
                deck, geometry = size_mos_instances(deck)
                deck, grid = windowed_transient(windowed_deck(deck), 1e-9)
                (case / 'physical.cir').write_text(deck)
                (case / 'bindings.json').write_text(json.dumps(bindings | dict(geometry=geometry), indent=2) + '\n')
                with capture_trace(case / 'spice.dat'):
                    command(case, 'ngspice', [args.ngspice.resolve(), '-b', 'physical.cir'])
                log = (case / 'ngspice.log').read_text()
                if re.search(r'\b(error|fatal|failed|aborted)\b|timestep too small', log, re.I):
                    raise ValueError('input-envelope process simulation failed')
                wave = case / 'spice.dat.gz'
                rows = read_states(wave, HEADER)
                result = contract(rows, code)
                result['observed_grid'] = check_observed_grid(rows, grid)
                report['cases'][key] = result | dict(waveform=str(wave), waveform_sha256=digest(wave))
                save()
                failed = [k for k, v in result['checks'].items() if not v]
                if failed:
                    raise ValueError('input envelope failed: ' + ', '.join(failed))
                print('PASS input envelope', key, result['pga_mean_range_V'], result['pga_terminal_range_V'], flush=True)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('input-envelope verification sources changed')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'ngspice', 'pdk', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    run(parser.parse_args())
