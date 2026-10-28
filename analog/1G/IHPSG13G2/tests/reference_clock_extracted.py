#!/usr/bin/env python3
"""Apply the unchanged PLL electrical contract to its filled, LVS-checked RC."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / 'VerilogA'), str(SOURCE.parents[1]), str(SOURCE.parent),
               str(ANALOG / '1G/tests'), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from compact_trace import read_spice
from extract_analog import digest, extract
from hardware import source_dependencies
from input_charge import verify_files
from reference_clock_extracted_process import netlist
from reference_clock_layout_process import observations, PORTS, HEADER
from reference_clock_physical_devices import deck
from reference_clock_contract import measure
from tool_process import configure_cleanup, execute
from trace_storage import archive_trace


def run(args):
    layout = json.loads(args.layout_proof.read_text())
    if (layout.get('status') != 'pass' or layout.get('full_drc_pass') is not True
            or layout.get('lvs', {}).get('instantiated_devices') != 526):
        raise ValueError('complete filled reference-clock DRC/LVS proof required')
    hashes = {}
    for key in ('source_sha256', 'artifact_sha256'):
        verify_files(layout[key])
        hashes.update(layout[key])
    filled = (Path(layout['work']) / 'reference.gds').resolve()
    gds = {p: h for p, h in layout['artifact_sha256'].items() if Path(p).resolve() == filled}
    if len(gds) != 1:
        raise ValueError('missing filled reference-clock GDS')
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    files = [SOURCE, args.layout_proof.resolve(), args.exporter.resolve(), args.ngspice.resolve(),
        SOURCE.with_name('reference_clock_physical_devices.py'), SOURCE.with_name('reference_clock_contract.py'),
        *[SOURCE.parents[1] / n for n in ('reference_clock_extracted_process.py', 'reference_clock_layout_process.py',
            'extract_analog.py', 'extraction_capacitance.py', 'extracted_connections.py', 'reduce_extracted_wires.py',
            'resistor_body_extraction.py', 'resistor_body_model.py', 'input_charge.py')],
        ANALOG / 'VerilogA/compact_trace.py', ANALOG / 'VerilogA/trace_storage.py',
        ANALOG / '1G/tests/hardware.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    files += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes.update({str(p): digest(p) for p in files})
    hashes.update(source_dependencies([SOURCE.with_name('reference_clock_layout_export.cpp')], ANALOG.parent,
        args.cxx, ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')]))
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, load_F=args.load, refined=args.refined,
        scope='filled-layout distributed RC, actual PSP/R3/MIM devices and finite supply/source impedance at TT; '
              'excludes independent RC calibration, PVT, mismatch and complete-chip qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        extraction = args.extraction.resolve() if args.extraction else work / 'extraction'
        if not args.extraction:
            if not args.magic:
                raise ValueError('--magic required when creating a fresh extraction')
            extract(next(iter(gds)), 'ReferenceClockGenerator', PORTS, args.pdk, args.magic, extraction)
        receipt = json.loads((extraction / 'extraction.json').read_text())
        if receipt.get('status') != 'pass' or receipt.get('ports') != PORTS:
            raise ValueError('conserved reference-clock extraction required')
        if any(receipt['source_sha256'].get(p) != h for p, h in gds.items()):
            raise ValueError('extraction and layout refer to different GDS')
        for key in ('source_sha256', 'artifact_sha256'):
            verify_files(receipt[key])
            hashes.update(receipt[key])
        hashes[str(extraction / 'extraction.json')] = digest(extraction / 'extraction.json')
        with (work / 'export.log').open('w') as log:
            execute([args.exporter.resolve(), work], stdout=log, stderr=-2, check=True, timeout=60,
                    memory_exclusive=False)
        model = SOURCE.parents[1] / 'generated/OneGIHPReferenceClockGenerator.va'
        if digest(work / 'reference.va') != digest(model):
            raise ValueError('C++ exporter differs from the physical circuit')
        logical = observations((work / 'reference.va').read_text(), json.loads((work / 'probes.json').read_text()))
        physical, probes, report['binding'] = netlist(extraction, layout['geometry'], logical)
        circuit = deck(physical, probes, models, args.load, args.refined)
        circuit = circuit.replace('Physical reference-clock devices, TT; ideal substrate contacts and no wiring RC',
                                  'Filled reference-clock extracted RC and physical devices, TT', 1)
        (work / 'physical.cir').write_text(circuit)
        hashes.update({str(work / n): digest(work / n) for n in ('physical.cir', 'reference.va', 'probes.json')})
        report['stage'] = 'transient'
        save()
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models.parent), OMP_NUM_THREADS='1')
        with (work / 'ngspice.log').open('w') as log:
            execute(['prlimit', '--as=1073741824', '--', args.ngspice.resolve(), '-b', 'physical.cir'],
                cwd=work, env=environment, stdout=log, stderr=-2, timeout=12000, check=True, memory_exclusive=False)
        trace = work / 'trace.dat'
        try:
            report['electrical'] = measure(read_spice(trace, ['time_s', *HEADER]), refined=args.refined)
        finally:
            if trace.exists():
                archive, report['trace_storage'] = archive_trace(trace)
                report['artifact_sha256'] = {str(archive): digest(archive)}
        verify_files(hashes)
        if report['electrical']['status'] != 'pass':
            raise ValueError('extracted PLL electrical limits failed: ' + str(report['electrical']['checks']))
        report.update(status='pass', stage='complete')
        print('PASS filled reference-clock RC electrical contract')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout-proof', 'exporter', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--extraction', type=Path)
    parser.add_argument('--magic', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--load', type=float, choices=(20e-15, 200e-15), default=20e-15)
    parser.add_argument('--refined', action='store_true')
    run(parser.parse_args())
