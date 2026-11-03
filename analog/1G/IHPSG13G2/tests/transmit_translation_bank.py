#!/usr/bin/env python3
"""Check simultaneous loading of all 22 actual TX translators and shared bias."""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / 'VerilogA'), str(SOURCE.parent),
               str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from compact_trace import checked_rows
from transmit_translation_bank import validate_export, deck
from transmit_translator_extracted import reference, compare, contracts
from transmit_translation_bias import reference as bias_reference
from transmit_translator_replica import merge_hashes
from resistor_body_extraction import adapt
from extract_analog import digest
from trace_storage import capture_trace
from hardware import source_dependencies
from tool_process import execute, configure_cleanup


def measure(path):
    with gzip.open(path, 'rt') as stream:
        header = next(stream).split()
        if len(header) != 93 or header[0] != 'time':
            raise ValueError('missing bank observation columns')
        rows = checked_rows((line.split() for line in stream if line.strip()), 93, str(path))
    if (not 0 <= rows[0][0] <= 1e-12 or abs(rows[-1][0] - 2.128e-6) > 1e-14 or
            any(b[0] - a[0] > 31.26e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete bank startup or switching trajectory')
    settled = [row[3] for row in rows if row[0] >= 1.6e-6]
    result = dict(bias_range_V=[min(settled), max(settled)], gates=[],
        checks=dict(bias_headroom=1.2 < min(settled) <= max(settled) < 2.3,
            bounded_bias_startup=all(-.1 < row[3] < 3.45 for row in rows)))
    for index in range(22):
        offset = 5 + 4 * index
        waveform = [[r[0], r[offset], r[offset + 1], r[offset + 2], r[1], r[2], r[4]]
                    for r in rows if r[0] >= 2e-6]
        gate = contracts(waveform, 1.2, 3.3, True, startup=2e-6)
        gate['maximum_lv_gate_difference_V'] = max(r[offset + 3] for r in rows)
        gate['checks']['lv_gate_exposure'] = (
            min(r[offset + 3] for r in rows) >= 0 and gate['maximum_lv_gate_difference_V'] <= 1.65)
        gate['status'] = 'pass' if all(gate['checks'].values()) else 'fail'
        result['gates'].append(gate)
    result['status'] = 'pass' if all(result['checks'].values()) and all(
        gate['status'] == 'pass' for gate in result['gates']) else 'fail'
    return result


def run(args):
    hashes = {}
    proofs = [json.loads(path.read_text()) for path in (args.translator, args.replica)]
    for proof in proofs:
        if proof.get('status') != 'pass':
            raise ValueError('passing component proofs required')
        for key in ('source_sha256', 'artifact_sha256'):
            merge_hashes(hashes, proof[key])
    if not proofs[0].get('missing_device_rejected') or not proofs[1].get('missing_replica_rejected'):
        raise ValueError('component fault-rejection evidence required')
    paths = [SOURCE, args.translator, args.replica, args.exporter, args.ngspice,
        SOURCE.parent / 'transmit_translator_extracted.py', SOURCE.parent / 'transmit_translation_bias.py',
        SOURCE.parent / 'transmit_translator_replica.py', SOURCE.parents[1] / 'transmit_translation_bank.py',
        ROOT / '1G/tests/level_shifter.py', ROOT / '1G/tests/core_transmit_fixture.hpp',
        ROOT / 'VerilogA/compact_trace.py', ROOT / 'VerilogA/trace_storage.py',
        ROOT / '1G/tests/hardware.py', ROOT / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    merge_hashes(hashes, {str(path.resolve()): digest(path) for path in paths})
    merge_hashes(hashes, source_dependencies([SOURCE.with_name('transmit_translation_bank_export.cpp')],
        ROOT.parent, args.cxx, ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include')]))
    def fresh():
        for path, value in hashes.items():
            if digest(path) != value:
                raise ValueError('changed bank evidence: ' + path)
    fresh()
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, flows={},
        corner=args.corner, temperature_C=args.temperature, translators=22,
        scope='C++-derived 22-translator shared-replica simultaneous-switching stress; '
              '2 pF/10 kohm per output, finite 0.25/2 ohm supplies with 20 pF decoupling; '
              'ideal inter-macro wires; excludes actual steering/trim loads, routed bank and chip qualification')
    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (args.work / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)
    try:
        save()
        with (work / 'export.log').open('w') as log:
            execute([args.exporter.resolve(), work / 'export'], stdout=log, stderr=-2, check=True,
                    timeout=120, memory_exclusive=False)
        tx, bias = proofs
        tx_work, bias_work = (Path(proof['work']) for proof in proofs)
        report['bank'] = validate_export(work / 'export', tx_work / 'export/translator.va',
                                        bias_work / 'export/translation_bias.va')
        hashes.update({str(path): digest(path) for path in (work / 'export').iterdir() if path.is_file()})
        physical_bias, report['replica_body_partition'] = adapt(
            (bias_work / 'extraction/named/capacitance.spice').read_text(),
            (bias_work / 'extraction/conserved.spice').read_text(),
            (bias_work / 'extraction/named/ExtractedAnalog.ext').read_text(), ['VDD', 'VSS', 'VLOGIC', 'BIAS'])
        physical_bias = physical_bias.replace('ExtractedAnalog', 'PhysicalBias')
        physical_tx = (tx_work / 'extraction/conserved.spice').read_text()
        schematic_tx = reference(work / 'export/translator_0.va', 'psp-charge')
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')
        for flow, translator, name, replica, bias_name in (
                ('schematic', schematic_tx, 'ReferenceTranslator', bias_reference(bias['geometry']), 'ReferenceBias'),
                ('physical', physical_tx, 'ExtractedAnalog', physical_bias, 'PhysicalBias')):
            directory = work / flow
            directory.mkdir()
            path = directory / 'bench.cir'
            path.write_text(deck(translator, name, replica, bias_name, models / 'models',
                                 args.corner, args.temperature))
            hashes[str(path)] = digest(path)
            report['stage'] = flow
            save()
            if args.emit_only:
                report['flows'][flow] = dict(status='netlist_only', deck=str(path), sha256=digest(path))
                continue
            with capture_trace(directory / 'trace.dat') as trace, (directory / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=3221225472', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                    cwd=directory, env=environment, stdout=log, stderr=-2, check=True, timeout=21600,
                    memory_exclusive=True)
            result = measure(trace.path)
            result['trace_storage'] = trace.metadata
            result['artifact_sha256'] = {str(path): digest(path), str(trace.path): digest(trace.path)}
            report['flows'][flow] = result
            fresh()
            save()
            print(flow, result['status'], result['bias_range_V'], flush=True)
        fresh()
        if args.emit_only:
            report['status'] = 'netlists_emitted'
            return
        if any(flow['status'] != 'pass' for flow in report['flows'].values()):
            raise ValueError('bank absolute electrical contract failed')
        report['comparison'] = [compare(a, b) for a, b in zip(
            report['flows']['schematic']['gates'], report['flows']['physical']['gates'])]
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('translator', 'replica', 'exporter', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--emit-only', action='store_true')
    run(parser.parse_args())
