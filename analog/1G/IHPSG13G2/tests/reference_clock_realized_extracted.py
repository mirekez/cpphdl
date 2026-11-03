#!/usr/bin/env python3
"""Qualify the filled native-cell PLL using its own conserved distributed RC."""
import argparse
import gzip
import json
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'VerilogA'),
               str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from compact_trace import checked_rows
from extract_analog import digest
from input_charge import verify_files
from named_node_extraction import extract_named
from reference_clock_realized_extracted import netlist
from reference_clock_layout_process import observations, PORTS, HEADER
from reference_clock_physical_devices import deck
from reference_clock_contract import measure
from tool_process import configure_cleanup, execute
from trace_storage import capture_trace


def layout_evidence(path):
    proof = json.loads(path.read_text())
    if (proof.get('status') != 'pass' or proof.get('filled') is not True or
            proof.get('full_drc_pass') is not True or proof.get('drc_violations') != {} or
            proof.get('lvs', {}).get('instantiated_devices') != 259 or
            proof.get('disconnected_reference_rejected') is not True or
            proof.get('geometry', {}).get('substrate_net') != 'VSS'):
        raise ValueError('filled native PLL DRC/LVS and disconnected-reference proof required')
    hashes = {}
    for group in ('source_sha256', 'artifact_sha256'):
        verify_files(proof[group])
        hashes.update(proof[group])
    filled = (Path(proof['work']) / 'clock.gds').resolve()
    artifacts = {Path(p).resolve(): h for p, h in proof['artifact_sha256'].items()}
    if filled not in artifacts:
        raise ValueError('filled native PLL GDS is not a verified artifact')
    return proof, filled, hashes


def run(args):
    layout, gds, hashes = layout_evidence(args.layout_proof)
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    files = [SOURCE, args.layout_proof.resolve(), args.exporter.resolve(), args.ngspice.resolve(),
        SOURCE.with_name('reference_clock_physical_devices.py'), SOURCE.with_name('reference_clock_contract.py')]
    files += [SOURCE.parents[1] / name for name in ('reference_clock_realized_extracted.py',
        'reference_clock_extracted_process.py', 'reference_clock_layout_process.py',
        'named_node_extraction.py', 'extract_analog.py', 'extraction_capacitance.py',
        'resistor_body_extraction.py', 'resistor_body_model.py', 'reduce_extracted_wires.py',
        'extracted_connections.py', 'input_charge.py')]
    files += [ANALOG / 'VerilogA' / name for name in ('compact_trace.py', 'trace_storage.py')]
    files += [ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    files += [p for p in models.rglob('*') if p.is_file() and
              (p.suffix in ('.lib', '.spice', '.osdi') or p.name == '.spiceinit')]
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        load_F=args.load, refined=args.refined,
        scope='filled native-cell PLL, conserved distributed RC, physical PSP/R3/MIM devices, '
              'finite supply and input impedance at TT/27C; PVT and full-chip qualification separate')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    try:
        report['stage'] = 'extraction'
        save()
        extraction = args.extraction.resolve() if args.extraction else work / 'extraction'
        if not args.extraction:
            if not args.magic:
                raise ValueError('--magic required for a fresh named extraction')
            extract_named(gds, 'RealizedReferenceClock', PORTS, args.pdk, args.magic,
                          extraction, substrate='VSS')
        receipt = json.loads((extraction / 'checks.json').read_text())
        if receipt.get('status') != 'pass' or receipt.get('inventory', {}).get('models') is None:
            raise ValueError('conserved named extraction required')
        sources = {Path(p).resolve(): h for p, h in receipt['source_sha256'].items()}
        if sources.get(gds) != digest(gds):
            raise ValueError('extraction does not belong to the verified filled GDS')
        for group in ('source_sha256', 'artifact_sha256'):
            verify_files(receipt[group])
            for path, value in receipt[group].items():
                if path in hashes and hashes[path] != value:
                    raise ValueError('conflicting extraction and layout provenance: ' + path)
                hashes[path] = value
        hashes[str(extraction / 'checks.json')] = digest(extraction / 'checks.json')
        with (work / 'export.log').open('w') as log:
            execute([args.exporter.resolve(), work], stdout=log, stderr=-2,
                    timeout=60, check=True, memory_exclusive=False)
        for name in ('reference.va', 'realizations.json'):
            if digest(work / name) != digest(Path(layout['work']) / name):
                raise ValueError('fresh C++ export differs from the verified geometry: ' + name)
        logical = observations((work / 'reference.va').read_text(), json.loads((work / 'probes.json').read_text()))
        circuit, probes, report['binding'] = netlist(extraction, layout['geometry'], logical)
        text = deck(circuit, probes, models / 'models', args.load, args.refined)
        text = 'Filled native-cell PLL with conserved distributed RC, TT/27C\n' + text.split('\n', 1)[1]
        (work / 'bench.cir').write_text(text)
        hashes.update({str(work / name): digest(work / name)
                       for name in ('bench.cir', 'reference.va', 'probes.json', 'realizations.json')})
        report['stage'] = 'transient'
        save()
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')
        with capture_trace(work / 'trace.dat') as trace:
            with (work / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=1073741824', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                    cwd=work, env=environment, stdout=log, stderr=-2, timeout=18000,
                    check=True, memory_exclusive=False)
        report['trace_storage'] = trace.metadata
        report['artifact_sha256'] = {str(trace.path): digest(trace.path)}
        report['stage'] = 'electrical-checks'
        save()
        with gzip.open(trace.path, 'rt') as stream:
            header = next(stream).split()
            if len(header) != len(HEADER) + 1 or header[0] != 'time':
                raise ValueError('PLL trace observation columns changed')
            rows = checked_rows((line.split() for line in stream if line.strip()), len(header), str(trace.path))
        report['electrical'] = measure(rows, refined=args.refined)
        verify_files(hashes)
        if report['electrical']['status'] != 'pass':
            raise ValueError('filled native PLL electrical limits failed: ' + str(report['electrical']['checks']))
        report.update(status='pass', stage='complete')
        print('PASS filled native PLL extracted electrical contract', flush=True)
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
    parser.add_argument('--magic', type=Path)
    parser.add_argument('--extraction', type=Path)
    parser.add_argument('--load', type=float, choices=(20e-15, 200e-15), default=20e-15)
    parser.add_argument('--refined', action='store_true')
    run(parser.parse_args())
