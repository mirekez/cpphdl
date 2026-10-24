#!/usr/bin/env python3
"""Check the low-headroom cascoded IHP transmitter and every solver state."""
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
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from cascoded_driver_process import process_deck
from mos_geometry import size_mos_instances
from spice_differential_vectors import keep_observed_differential_voltages
from core_transmit_driver import HEADER, TRIM_HEADER, qualified_controls
from core_transmit_states import fresh
from transmit_driver import controls, digest
from transmit_state_contract import check
from hardware import configure_cleanup, execute, source_dependencies
from trace_states import read_states
from trace_storage import capture_trace


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='states-', dir=root))
    stimulus = qualified_controls(args.fixture.resolve())
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, SOURCE.parents[1] / 'cascoded_driver_process.py', SOURCE.parents[1] / 'core_driver_process.py', SOURCE.parents[1] / 'mos_geometry.py',
             SOURCE.parents[1] / 'spice_differential_vectors.py', args.bench.resolve(), args.ngspice.resolve(),
             args.fixture.resolve(), stimulus, models / '.spiceinit']
    paths += [ANALOG / '1G/tests' / name for name in
              ('core_transmit_driver.py', 'core_transmit_states.py', 'transmit_driver.py', 'transmit_state_contract.py')]
    paths += [ANALOG / 'VerilogA' / name for name in ('compare.py', 'trace_states.py', 'trace_codec.py', 'trace_storage.py')]
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes |= {str(path): digest(path) for path in paths}
    report = dict(status='running', flow='psp', corner=args.corner, temperature_C=args.temperature,
                  trim_code=args.trim_code, source_sha256=hashes, work=str(work),
                  scope='complete loaded 304-MOS cascoded IHP PSP bank at declared corner/temperature/trim, nominal passives; no automatic calibration/layout/full-chip qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (root / 'checks.json').write_text(text)

    save()
    try:
        with (work / 'export.log').open('w') as log:
            command = [str(args.bench.resolve()), str(work), 'export', str(stimulus), '100', '0']
            if args.trim_code is not None:
                command.append(str(args.trim_code))
            execute(command, stdout=log, stderr=-2, check=True, timeout=120)
        shutil.copyfile(stimulus, work / 'controls.csv')
        deck, bindings = process_deck((work / 'design.va').read_text(), (work / 'bench.cir').read_text(),
            models / 'models', args.corner, programmable=args.trim_code is not None, temperature=args.temperature)
        deck, geometry = size_mos_instances(deck)
        (work / 'device_bindings.json').write_text(json.dumps(bindings, indent=2) + '\n')
        (work / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        (work / 'ihp.cir').write_text(keep_observed_differential_voltages(deck))
        wave = work / 'spice.dat.gz'
        if args.reuse_report is not None:
            old = json.loads(args.reuse_report.read_text())
            if old.get('status') != 'failed' or old.get('error') != '[Errno 28] No space left on device':
                raise ValueError('only completed storage-failed process data can be recovered')
            fresh(old['source_sha256'])
            if (old['corner'], old['temperature_C'], old['trim_code']) != (args.corner, args.temperature, args.trim_code):
                raise ValueError('retained process profile differs')
            old_work = Path(old['work'])
            for name in ('ihp.cir', 'device_bindings.json', 'mos_geometry.json', 'controls.csv', 'interface.json',
                         'bench.cir', 'design.va', 'conditions.json', 'columns.csv'):
                if digest(work / name) != digest(old_work / name):
                    raise ValueError('regenerated process experiment differs: ' + name)
            source = old_work / 'spice.dat.gz'
            if source.stat().st_dev == work.stat().st_dev:
                os.link(source, wave)
            else:
                shutil.copyfile(source, wave)
            shutil.copyfile(old_work / 'ngspice.log', work / 'ngspice.log')
            report['reused_simulation'] = dict(receipt=str(args.reuse_report.resolve()),
                receipt_sha256=digest(args.reuse_report), waveform_sha256=digest(source),
                scope='complete CRC-checked trajectory recovered from output-storage failure; no new transient simulation')
        else:
            environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                               SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
            with capture_trace(work / 'spice.dat'), (work / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'ihp.cir'], cwd=work, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=3600)
        log = (work / 'ngspice.log').read_text()
        if ('ngspice-47 done' not in log or re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small', log, re.I)):
            raise ValueError('IHP loaded bank simulation failed or incomplete')
        interface = json.loads((work / 'interface.json').read_text())
        if interface.get('trim_code') != args.trim_code or interface['load_ohm'] != 100 or not interface['filtered']:
            raise ValueError('incorrect process transmitter experiment')
        header = HEADER + TRIM_HEADER if args.trim_code is not None else HEADER
        rows = read_states(wave, header)
        counts = re.findall(r'No[.] of Data Rows\s*:\s*(\d+)', log)
        if counts != [str(len(rows))]:
            raise ValueError('captured process stream differs from simulator row count')
        report.update(check(rows, controls(work / 'controls.csv'), interface))
        report['retained_rows'] = len(rows)
        report['equal_time_records'] = sum(a[0] == b[0] for a, b in zip(rows, rows[1:]))
        report['artifact_sha256'] = {str(path): digest(path) for path in work.iterdir()
                                   if path.is_file() and path.name != 'checks.json'}
        fresh(hashes)
        report['status'] = 'pass'
        print('PASS', args.corner, args.temperature, 'C; trim', args.trim_code,
              '; maximum error V', report['maximum_all_state_plateau_error_V'], flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'fixture', 'fixture-source', 'bench', 'pdk', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--trim-code', type=int, choices=range(64))
    parser.add_argument('--reuse-report', type=Path)
    run(parser.parse_args())
