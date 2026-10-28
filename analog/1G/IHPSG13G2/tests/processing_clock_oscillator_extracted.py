#!/usr/bin/env python3
"""Apply oscillator acceptance to the conserved physical distributed RC network."""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parent), str(ANALOG / 'VerilogA'),
               str(ANALOG / '1G/tests'), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from oscillator_extracted_process import netlist
from processing_clock_oscillator_physical_devices import deck
from processing_clock_oscillator import measure, HEADER, CASES
from processing_clock_oscillator_layout import PORTS
from compact_trace import read_spice
from extract_analog import digest
from input_charge import verify_files
from tool_process import execute, configure_cleanup


def run(args):
    cases, measurement, centre_code = CASES, measure, '38528'
    if args.acquisition_window:
        from oscillator_acquisition_window import CASES as window_cases, measure as window_measure, STOP
        cases, measurement, centre_code = window_cases, window_measure, '45056'
    extraction = args.extraction.resolve()
    receipt = json.loads((extraction / 'extraction.json').read_text())
    layout = json.loads(args.layout_proof.read_text())
    if (receipt.get('status') != 'pass' or receipt.get('ports') != PORTS
            or layout.get('status') != 'pass' or layout.get('lvs', {}).get('instantiated_devices') != 294):
        raise ValueError('requires checked oscillator geometry and conserved extraction')
    hashes = {}
    for record in (receipt, layout):
        for field in ('source_sha256', 'artifact_sha256'):
            verify_files(record[field])
            hashes.update(record[field])
    gds = {p: h for p, h in layout['artifact_sha256'].items() if Path(p).name == 'oscillator.gds'}
    if len(gds) != 1 or any(receipt['source_sha256'].get(p) != h for p, h in gds.items()):
        raise ValueError('layout and extraction describe different GDS')
    model = SOURCE.parents[1] / 'generated/OneGIHPProcessingClockOscillator.va'
    physical, probes, audit = netlist(extraction, model)
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    files = [SOURCE, args.layout_proof.resolve(), extraction / 'extraction.json',
        SOURCE.parents[1] / 'oscillator_extracted_process.py', SOURCE.parents[1] / 'reduce_extracted_wires.py',
        SOURCE.parents[1] / 'extracted_connections.py',
        SOURCE.parents[1] / 'resistor_body_model.py', SOURCE.parents[1] / 'resistor_body_extraction.py',
        SOURCE.with_name('processing_clock_oscillator_physical_devices.py'), SOURCE.with_name('processing_clock_oscillator.py'),
        ANALOG / 'VerilogA/compact_trace.py', ANALOG / 'VerilogA/compare.py',
        SOURCE.parents[1] / 'input_charge.py', args.ngspice.resolve()]
    files += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    if args.acquisition_window:
        files += [SOURCE.with_name('oscillator_acquisition_window.py'), ANALOG / '1G/processing_clock_control.hpp']
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={}, binding=audit,
        acquisition_window=args.acquisition_window,
        scope='TT fixed-trim extracted oscillator with exact resistor-only nodal elimination; excludes field-solver calibration, '
              'closed-loop acquisition, noise, mismatch, PVT and full-chip qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        for name, (code, load, refined) in cases.items():
            case = work / name
            case.mkdir()
            stimulus = deck(physical, models, code, load, refined, probes)
            if args.acquisition_window:
                maximum = 5e-12 if refined else 10e-12
                stimulus, count = re.subn(r'^tran .+$',
                    f'tran {maximum / 2:.17g} {STOP:.17g} 0 {maximum:.17g} uic', stimulus, flags=re.M)
                if count != 1:
                    raise ValueError('changed oscillator transient command')
            (case / 'bench.cir').write_text(stimulus)
            environment = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models.parent))
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case, env=environment,
                    stdout=log, stderr=-2, timeout=2400, check=True, memory_exclusive=False)
            trace = case / 'trace.dat'
            metrics = measurement(read_spice(trace, HEADER), refined)
            with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
                shutil.copyfileobj(source, target)
            trace.unlink()
            metrics.update(parameters=dict(trim=code, load_F=load, refined=refined), artifact_sha256={
                str(case / 'bench.cir'): digest(case / 'bench.cir'), str(trace) + '.gz': digest(str(trace) + '.gz')})
            report['cases'][name] = metrics
            save()
            if metrics['status'] != 'pass':
                raise ValueError('extracted oscillator failed: ' + name + ' ' + str(metrics['checks']))
        rates = [report['cases'][name]['frequency_Hz'] for name in cases if name.isdecimal()]
        if not (rates[0] < 132e6 and rates[-1] > 136e6 and all(a < b for a, b in zip(rates, rates[1:]))):
            raise ValueError('extracted trim range does not bracket processing-clock band')
        centre = report['cases'][centre_code]['frequency_Hz']
        for name, limit in [('loaded', .02), ('refined', .005)]:
            if abs(report['cases'][name]['frequency_Hz'] / centre - 1) > limit:
                raise ValueError('extracted load/refinement sensitivity exceeded: ' + name)
        verify_files(hashes)
        report.update(status='pass', monotonic_range_Hz=[rates[0], rates[-1]], required_band_Hz=[132e6, 136e6])
        print('PASS extracted oscillator: ' + ('controller measurement window' if args.acquisition_window else 'early settling stress'))
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('extraction', 'layout-proof', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--acquisition-window', action='store_true',
        help='separate cold-start measurement after two 128-cycle settling windows; keeps early stress unchanged')
    run(parser.parse_args())
