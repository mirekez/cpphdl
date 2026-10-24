#!/usr/bin/env python3
"""Check the complete loaded transmit bank with explicitly sized IHP PSP MOS."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path.insert(0, str(SOURCE.parents[1]))
from core_driver_process import process_deck
from mos_geometry import size_mos_instances
from spice_differential_vectors import keep_observed_differential_voltages
sys.path.insert(0, str(ANALOG / '1G/tests'))
from core_transmit_driver import HEADER, TRIM_HEADER, check, qualified_controls, read_trace
from transmit_driver import controls, digest
from hardware import configure_cleanup, execute, source_dependencies
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from trace_codec import normalize_roundoff_duplicates, verify_roundoff_audit
from trace_storage import capture_trace


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    report_path = root / 'checks.json'
    report_path.unlink(missing_ok=True)
    fixture = args.fixture.resolve()
    controller = qualified_controls(fixture)
    technology = args.pdk.resolve() / 'ihp-sg13g2'
    models = technology / 'libs.tech/ngspice'
    paths = [SOURCE, SOURCE.parents[1] / 'core_driver_process.py', SOURCE.parents[1] / 'mos_geometry.py', SOURCE.parents[1] / 'spice_differential_vectors.py',
             ANALOG / '1G/tests/core_transmit_driver.py', ANALOG / '1G/tests/transmit_driver.py',
             ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/trace_codec.py', ANALOG / 'VerilogA/trace_storage.py',
             args.bench.resolve(), args.ngspice.resolve(), fixture, controller, models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(path): digest(path) for path in paths})
    report = dict(status='running', corner=args.corner, temperature_C=args.temperature,
                  trim_code=args.trim_code, fixture_source=str(args.fixture_source.resolve()), source_sha256=hashes,
                  work=str(work), solver_thread_limit=1,
                  scope='IHP PSP loaded bank at declared MOS corner, temperature and trim; nominal passives, no automatic calibration or layout/full-chip qualification')
    try:
        with (work / 'export.log').open('w') as log:
            command = [str(args.bench.resolve()), str(work), 'export', str(controller), '100', '0']
            if args.trim_code is not None:
                command.append(str(args.trim_code))
            execute(command,
                    stdout=log, stderr=-2, check=True, timeout=60)
        shutil.copyfile(controller, work / 'controls.csv')
        deck, bindings = process_deck((work / 'design.va').read_text(), (work / 'bench.cir').read_text(),
                                      models / 'models', args.corner,
                                      programmable=args.trim_code is not None, temperature=args.temperature)
        deck, geometry = size_mos_instances(deck)
        (work / 'device_bindings.json').write_text(json.dumps(bindings, indent=2) + '\n')
        (work / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        deck = keep_observed_differential_voltages(deck)
        (work / 'ihp.cir').write_text(deck)
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
        with capture_trace(work / 'spice.dat') as captured, (work / 'ngspice.log').open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', 'ihp.cir'], cwd=work, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=2400)
        if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small',
                     (work / 'ngspice.log').read_text(), re.I):
            raise ValueError('IHP loaded transmit bank simulation failed')
        trace = captured.path
        audit = normalize_roundoff_duplicates(trace)
        if json.loads(audit.read_text())['original_sha256'] != captured.metadata['original_sha256']:
            raise ValueError('roundoff audit does not reconstruct the captured process stream')
        interface = json.loads((work / 'interface.json').read_text())
        if interface.get('trim_code') != args.trim_code:
            raise ValueError('exported transmitter trim differs from requested circuit')
        header = HEADER + TRIM_HEADER if args.trim_code is not None else HEADER
        result = check(read_trace(trace, 'veriloga', header), controls(work / 'controls.csv'), interface)
        report.update(result)
        archive = trace
        report['original_stream_sha256'] = captured.metadata['original_sha256']
        report['roundoff_duplicate_count'] = verify_roundoff_audit(archive, audit)
        report['artifact_sha256'] = {str(path): digest(path) for path in
            (archive, audit, work / 'ihp.cir', work / 'device_bindings.json', work / 'mos_geometry.json',
             work / 'controls.csv', work / 'interface.json', work / 'ngspice.log')}
        if any(digest(Path(path)) != value for path, value in hashes.items()):
            raise ValueError('IHP loaded-bank input changed during qualification')
        report['status'] = 'pass'
        print('PASS IHP PSP loaded bank;', sum(bindings['roles'].values()),
              'physical MOS, shared bias and both finite supplies')
    except BaseException as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        report_path.write_text(text)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--fixture-source', type=Path, default=ANALOG / '1G/tests/core_transmit_driver.cpp')
    parser.add_argument('--trim-code', type=int, choices=range(64))
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    run(parser.parse_args())
