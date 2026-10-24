#!/usr/bin/env python3
"""Check the sixteen-slice receiver with IHP PSP transistors and continuous warmup."""
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
from programmable_array_process import process_deck
from mos_geometry import size_mos_instances
from programmable_receive_array import HEADER, contract, digest, verify, windowed_deck, read_spice
CODES = (4, 5, 6, 7, 8)
from hardware import configure_cleanup, execute, source_dependencies
from compare import errors as directional_errors
from time_grid import windowed_transient, check_observed_grid
from trace_storage import archive_trace, capture_trace


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    tracked = [SOURCE, SOURCE.with_name('programmable_receive_array.py'), SOURCE.with_name('programmable_receive_gain.py'),
               SOURCE.parents[1] / 'programmable_array_process.py', SOURCE.parents[1] / 'receive_frontend_process.py',
               SOURCE.parents[1] / 'mos_geometry.py', SOURCE.parents[1] / 'spice_vectors.py',
               ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/time_grid.py', ANALOG / 'VerilogA/trace_storage.py', args.bench.resolve(),
               args.ngspice.resolve(), models / '.spiceinit']
    tracked += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', corner=args.corner, temperature_C=args.temperature, source_sha256=hashes,
        cases={}, run_directory=str(work), fixture_source=str(args.fixture_source.resolve()), same_equations_as_square_law=False, warmup_maximum_step_s=args.warmup_step,
        initialization=args.initialization, sampler_dummy_p_width_um=args.dummy_p_width_um,
        charge_defined_nodes=args.charge_defined_nodes,
        scope='actual IHP PSP MOS with declared external R/C and fixed host controls; sixteen 1.5 pF samplers, 64 acquisitions and fixed 100 ohm termination; no ADC conversion, AGC, noise or layout proof')
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
                models / 'models', args.corner, args.temperature, dummy_p_width_um=args.dummy_p_width_um,
                charge_defined_nodes=args.charge_defined_nodes)
            deck, geometry = size_mos_instances(deck)
            deck, timing_grid = windowed_transient(windowed_deck(deck), args.warmup_step)
            if args.initialization == 'bias':
                deck, changed = re.subn(r'(?m)^(tran [^\n]+) uic$', r'\1', deck)
                if changed != 1:
                    raise ValueError('changed array PSP initialization interface')
            timing_grid['initialization'] = args.initialization
            timing_grid['windowed_deck_sha256'] = hashlib.sha256(deck.encode()).hexdigest()
            (case / 'physical.cir').write_text(deck)
            (case / 'timing-grid.json').write_text(json.dumps(timing_grid, indent=2) + '\n')
            (case / 'bindings.json').write_text(json.dumps(bindings, indent=2) + '\n')
            (case / 'geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
            with capture_trace(case / 'spice.dat') as captured, (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'physical.cir'], cwd=case, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=7200)
            if re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small',
                         (case / 'ngspice.log').read_text(), re.I):
                raise ValueError('programmable receiver PSP simulation failed')
            raw = captured.path
            rows = read_spice(raw, HEADER)
            result = contract(rows, code)
            result['observed_grid'] = check_observed_grid(rows, timing_grid)
            result.update(waveform=str(raw), waveform_archive=captured.metadata)
            result['artifact_sha256'] = {str(p): digest(p) for p in case.iterdir() if p.is_file()}
            report['cases'][str(code)] = result
            # Failed purpose checks retain every original trace byte too.
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


def compare(args):
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.unlink(missing_ok=True)
    roots = [path.resolve() for path in args.compare]
    reports = [json.loads((root / 'checks.json').read_text()) for root in roots]
    if (any(report.get('status') != 'pass' or set(report['cases']) != set(map(str, args.codes)) for report in reports)
            or str(SOURCE) not in reports[0].get('source_sha256', {})
            or reports[0]['source_sha256'] != reports[1]['source_sha256']
            or any((report['corner'], report['temperature_C']) != (reports[0]['corner'], reports[0]['temperature_C']) for report in reports)
            or any(report.get('initialization') != reports[0].get('initialization') for report in reports)
            or any(report.get('fixture_source') != reports[0].get('fixture_source') for report in reports)
            or not reports[0].get('initialization') or not reports[0].get('fixture_source')
            or len({report['warmup_maximum_step_s'] for report in reports}) != 2):
        raise ValueError('missing independent same-model warmup-refinement flows')
    for report in reports:
        for path, expected in report['source_sha256'].items():
            if digest(path) != expected:
                raise ValueError('changed process-refinement input: ' + path)
    result = dict(status='running', cases={}, scope='PSP warmup-step convergence; same physics, continuous state and full observation window')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.unlink(missing_ok=True)
    for code in args.codes:
        waves = []
        for report in reports:
            case = Path(report['run_directory']) / str(code)
            artifacts = report['cases'][str(code)]['artifact_sha256']
            raw = Path(report['cases'][str(code)]['waveform'])
            if not {str(raw), str(case / 'timing-grid.json')} <= artifacts.keys():
                raise ValueError('missing waveform or timing-grid provenance')
            for path, expected in artifacts.items():
                if digest(path) != expected:
                    raise ValueError('changed process-refinement artifact: ' + path)
            rows = read_spice(raw, HEADER)
            verify(contract(rows, code))
            grid = json.loads((case / 'timing-grid.json').read_text())
            if (grid['warmup_maximum_step_s'] != report['warmup_maximum_step_s']
                    or grid.get('initialization') != report['initialization']):
                raise ValueError('refinement report does not match its actual integration grid')
            check_observed_grid(rows, grid)
            waves.append(rows)
        unique = [[row for index, row in enumerate(rows) if index == 0 or row[0] != rows[index - 1][0]] for rows in waves]
        peak = [max(a, b) for a, b in zip(directional_errors(waves[0], unique[1]), directional_errors(waves[1], unique[0]))]
        result['cases'][str(code)] = dict(peak_differences_V=dict(zip(HEADER[1:], peak)))
        if max(peak) > .001:
            raise ValueError('process warmup refinement differs by more than 1 mV')
    result.update(status='pass', flow_report_sha256=[digest(root / 'checks.json') for root in roots])
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS independent PSP checks and 1 mV warmup-refinement comparison')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--fixture-source', type=Path, default=SOURCE.with_name('programmable_receive_array.cpp'))
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--codes', nargs='+', type=int, choices=CODES, default=[4, 8])
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--warmup-step', type=float, default=1e-9)
    parser.add_argument('--initialization', choices=('uic', 'bias'), default='uic')
    parser.add_argument('--dummy-p-width-um', type=int, choices=(25, 30), default=25)
    parser.add_argument('--charge-defined-nodes', action='store_true')
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--compare', type=Path, nargs=2)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if len(set(args.codes)) != len(args.codes):
        parser.error('receive gain codes must be unique')
    if args.compare:
        if args.output is None:
            parser.error('comparison requires an output report')
        compare(args)
    else:
        if any(value is None for value in (args.bench, args.pdk, args.ngspice, args.work)):
            parser.error('process run requires its bench, PDK, simulator and work directory')
        run(args)
