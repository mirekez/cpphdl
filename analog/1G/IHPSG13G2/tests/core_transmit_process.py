#!/usr/bin/env python3
"""Check the complete loaded transmit bank with explicitly sized IHP PSP MOS."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path.insert(0, str(SOURCE.parents[1]))
from core_driver_process import process_deck
from mos_geometry import size_mos_instances
from spice_differential_vectors import keep_observed_differential_voltages
sys.path.insert(0, str(ANALOG / '1G/tests'))
from core_transmit_driver import HEADER, check
from transmit_driver import controls, digest
from hardware import configure_cleanup, execute, source_dependencies
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import read_spice
from trace_codec import normalize_roundoff_duplicates, verify_roundoff_audit


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    fixture = args.fixture.resolve()
    evidence = json.loads(fixture.read_text())
    controller = fixture.parent / 'nominal/controls.csv'
    if (evidence.get('status') != 'pass' or evidence.get('flow') != 'cpp'
            or evidence['cases']['nominal']['controls_sha256'] != digest(controller)):
        raise ValueError('IHP transmit bank requires its qualified actual controller trace')
    for name, value in (evidence['source_sha256'] | evidence['binary_sha256']).items():
        if digest(Path(name)) != value:
            raise ValueError('stale qualified controller fixture: ' + name)
    technology = args.pdk.resolve() / 'ihp-sg13g2'
    models = technology / 'libs.tech/ngspice'
    paths = [SOURCE, SOURCE.parents[1] / 'core_driver_process.py', SOURCE.parents[1] / 'mos_geometry.py', SOURCE.parents[1] / 'spice_differential_vectors.py',
             ANALOG / '1G/tests/core_transmit_driver.py', ANALOG / '1G/tests/transmit_driver.py',
             ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/trace_codec.py',
             args.bench.resolve(), args.ngspice.resolve(), fixture, controller, models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([ANALOG / '1G/tests/core_transmit_driver.cpp'], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(path): digest(path) for path in paths})
    report = dict(status='running', corner=args.corner, source_sha256=hashes,
                  scope='nominal IHP PSP loaded sixteen-translator bank and weighted driver; no full PVT, mismatch or layout qualification')
    try:
        with (work / 'export.log').open('w') as log:
            execute([str(args.bench.resolve()), str(work), 'export', str(controller), '100', '0'],
                    stdout=log, stderr=-2, check=True, timeout=60)
        shutil.copyfile(controller, work / 'controls.csv')
        deck, bindings = process_deck((work / 'design.va').read_text(), (work / 'bench.cir').read_text(),
                                      models / 'models', args.corner)
        deck, geometry = size_mos_instances(deck)
        (work / 'device_bindings.json').write_text(json.dumps(bindings, indent=2) + '\n')
        (work / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        deck = keep_observed_differential_voltages(deck)
        (work / 'ihp.cir').write_text(deck)
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models))
        with (work / 'ngspice.log').open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', 'ihp.cir'], cwd=work, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=2400)
        if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small',
                     (work / 'ngspice.log').read_text(), re.I):
            raise ValueError('IHP loaded transmit bank simulation failed')
        trace = work / 'spice.dat'
        audit = normalize_roundoff_duplicates(trace)
        result = check(read_spice(trace, HEADER), controls(work / 'controls.csv'),
                       json.loads((work / 'interface.json').read_text()))
        report.update(result)
        archive = work / 'spice.dat.gz'
        original = digest(trace)
        with trace.open('rb') as source, gzip.open(archive, 'wb', compresslevel=6) as output:
            shutil.copyfileobj(source, output)
        restored = hashlib.sha256()
        with gzip.open(archive, 'rb') as source:
            for block in iter(lambda: source.read(1024 * 1024), b''):
                restored.update(block)
        if restored.hexdigest() != original:
            raise ValueError('IHP loaded-bank waveform archive is not lossless')
        trace.unlink()
        report['roundoff_duplicate_count'] = verify_roundoff_audit(archive, audit)
        report['artifact_sha256'] = {str(path): digest(path) for path in
            (archive, audit, work / 'ihp.cir', work / 'device_bindings.json', work / 'mos_geometry.json',
             work / 'controls.csv', work / 'interface.json', work / 'ngspice.log')}
        if any(digest(Path(path)) != value for path, value in hashes.items()):
            raise ValueError('IHP loaded-bank input changed during qualification')
        report['status'] = 'pass'
        print('PASS nominal IHP PSP loaded bank; 218 physical MOS, shared bias and both finite supplies')
    except BaseException as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), default='mos_tt')
    run(parser.parse_args())
