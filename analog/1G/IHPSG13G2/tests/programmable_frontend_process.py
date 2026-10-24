#!/usr/bin/env python3
"""Check the buffered programmable receiver with explicit IHP PSP dimensions and fixed cable termination."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from programmable_frontend_process import process_deck
from mos_geometry import size_mos_instances
from programmable_receive_frontend import HEADER, contract, digest, verify, windowed_deck
CODES = (4, 5, 6, 7, 8)
from hardware import configure_cleanup, execute, source_dependencies
from compare import read_spice
from trace_codec import normalize_roundoff_duplicates, verify_roundoff_audit
from time_grid import windowed_transient, check_observed_grid


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    tracked = [SOURCE, SOURCE.with_name('programmable_receive_frontend.py'), SOURCE.with_name('programmable_receive_gain.py'),
               SOURCE.parents[1] / 'programmable_frontend_process.py', SOURCE.parents[1] / 'receive_frontend_process.py',
               SOURCE.parents[1] / 'mos_geometry.py', SOURCE.parents[1] / 'spice_vectors.py',
               ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/trace_codec.py', ANALOG / 'VerilogA/time_grid.py', args.bench.resolve(),
               args.ngspice.resolve(), models / '.spiceinit']
    tracked += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', corner=args.corner, temperature_C=args.temperature, source_sha256=hashes,
        cases={}, run_directory=str(work), same_equations_as_square_law=False,
        warmup_maximum_step_s=args.warmup_step, initialization=args.initialization,
        fixture_source=str(args.fixture_source.resolve()),
        scope='actual IHP PSP MOS with declared external R/C and fixed host controls; fixed 100 ohm termination and sampler acquisition; no ADC conversion, AGC, noise or layout proof')
    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)
    save()
    try:
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
        for code in args.codes:
            case = work / str(code)
            case.mkdir()
            with (case / 'export.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'export', str(code)],
                        stdout=log, stderr=-2, check=True, timeout=60)
            deck, bindings = process_deck((case / 'design.va').read_text(), (case / 'bench.cir').read_text(),
                models / 'models', args.corner, args.temperature)
            deck, geometry = size_mos_instances(deck)
            deck = windowed_deck(deck)
            grid = None
            if args.warmup_step is not None:
                deck, grid = windowed_transient(deck, args.warmup_step)
            if args.initialization == 'bias':
                # Constrain the original declared voltages while initializing
                # intrinsic PSP charge with the bias solution.
                deck, changed = re.subn(r'(?m)^(tran [^\n]+) uic$', r'\1', deck)
                if changed != 1:
                    raise ValueError('changed PSP initialization interface')
            if grid is not None:
                grid['initialization'] = args.initialization
                grid['windowed_deck_sha256'] = hashlib.sha256(deck.encode()).hexdigest()
                (case / 'timing-grid.json').write_text(json.dumps(grid, indent=2) + '\n')
            (case / 'physical.cir').write_text(deck)
            (case / 'bindings.json').write_text(json.dumps(bindings, indent=2) + '\n')
            (case / 'geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'physical.cir'], cwd=case, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=1800)
            if re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small',
                         (case / 'ngspice.log').read_text(), re.I):
                raise ValueError('programmable receiver PSP simulation failed')
            raw = case / 'spice.dat'
            audit = normalize_roundoff_duplicates(raw)
            rows = read_spice(raw, HEADER)
            result = contract(rows, code)
            if grid is not None:
                result['observed_grid'] = check_observed_grid(rows, grid)
            result['roundoff_duplicate_count'] = verify_roundoff_audit(raw, audit)
            result['artifact_sha256'] = {str(p): digest(p) for p in case.iterdir() if p.is_file()}
            report['cases'][str(code)] = result
            save()
            verify(result)
            print('PASS PSP code', code, 'gain', result['gain'], 'residual V', result['maximum_linearity_residual_V'], 'acquisition V', result['maximum_acquisition_error_V'], flush=True)
        gains = [-report['cases'][str(code)]['gain'] for code in sorted(args.codes)]
        if any(a >= b for a, b in zip(gains, gains[1:])):
            raise ValueError('receive gain masks are not monotonic')
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('programmable receiver process inputs changed during qualification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--fixture-source', type=Path, default=SOURCE.with_name('programmable_receive_frontend.cpp'))
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--codes', nargs='+', type=int, choices=CODES, default=list(CODES))
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--initialization', choices=('uic', 'bias'), default='uic', help='bias initializes intrinsic PSP charge while retaining original voltage constraints')
    parser.add_argument('--warmup-step', type=float, help='optional quiet-warmup maximum step; fine measurement grid and electrical limits stay fixed')
    args = parser.parse_args()
    if len(set(args.codes)) != len(args.codes):
        parser.error('receive gain codes must be unique')
    run(args)
