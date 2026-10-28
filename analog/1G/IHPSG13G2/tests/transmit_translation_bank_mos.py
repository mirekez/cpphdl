"""Load the qualified HV-storage replica with all 22 actual TX translators."""
import argparse
import json
import os
from pathlib import Path
import runpy
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / 'VerilogA'), str(SOURCE.parent),
               str(ROOT / '1G/tests'), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from extract_analog import digest
from hardware import source_dependencies
from input_charge import verify_files
from transmit_translation_bank import validate_export, deck
from transmit_translator_extracted import reference, compare
from transmit_translation_bias_mos_extracted import storage_reference
from reserved_trace import capture_reserved_trace
from tool_process import configure_cleanup, execute, memory_slot

measure = runpy.run_path(str(SOURCE.with_name('transmit_translation_bank.py')))['measure']


def run(args):
    hashes, proofs = {}, {}

    def merge(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key in ('source_sha256', 'artifact_sha256'):
                    verify_files(child)
                    for path, sha in child.items():
                        if path in hashes and hashes[path] != sha:
                            raise ValueError('conflicting MOS replica evidence')
                        hashes[path] = sha
                else:
                    merge(child)
        elif isinstance(value, list):
            for child in value:
                merge(child)

    for name in ('translator', 'replica'):
        path = getattr(args, name).resolve()
        proof = json.loads(path.read_text())
        if proof.get('status') != 'pass':
            raise ValueError('passing component proof required: ' + name)
        merge(proof)
        hashes[str(path)] = digest(path)
        proofs[name] = proof
    tx, bias = proofs['translator'], proofs['replica']
    if (not tx['missing_device_rejected'] or bias['geometry']['storage_realization'] != 'hv_gate_candidate'
            or set(bias['electrical']) != {'mos_tt', 'mos_ss', 'mos_ff'}):
        raise ValueError('qualified translator and three-corner MOS replica required')
    for corner in bias['electrical'].values():
        if any(not all(corner[flow]['checks'].values()) for flow in ('physical', 'schematic')):
            raise ValueError('replica absolute checks failed')
        if corner['comparison']['maximum_settled_relative_error'] > .05:
            raise ValueError('replica comparison failed')

    def read(path):
        path = Path(path).resolve()
        if not any(Path(p).resolve() == path and digest(path) == sha for p, sha in hashes.items()):
            raise ValueError('unqualified bank input: ' + str(path))
        return path.read_text()

    tx_work = Path(tx['work'])
    physical_tx = read(tx_work / 'extraction/conserved.spice')
    physical_bias = read(bias['physical_model']).replace('ExtractedAnalog', 'PhysicalBias')
    read(bias['exported_source'])
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, SOURCE.with_name('transmit_translation_bank.py'), args.exporter.resolve(),
             args.ngspice.resolve(), models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    for module in list(sys.modules.values()):
        filename = getattr(module, '__file__', None)
        if filename:
            path = Path(filename).resolve()
            if path.is_relative_to(ROOT) and 'build' not in path.relative_to(ROOT).parts:
                paths.append(path)
    for path in paths:
        value = digest(path)
        if str(path) in hashes and hashes[str(path)] != value:
            raise ValueError('stale loaded-bank input')
        hashes[str(path)] = value
    for path, value in source_dependencies([SOURCE.with_name('transmit_translation_bank_export.cpp')],
            ROOT.parent, args.cxx, ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include')]).items():
        if path in hashes and hashes[path] != value:
            raise ValueError('stale bank exporter source')
        hashes[path] = value
    report = dict(status='running', stage='export', work=str(work), source_sha256=hashes,
        flows={}, corner=args.corner, temperature_C=args.temperature,
        scope='22-translator simultaneous-switching stress with nonlinear HV replica storage; '
              'same independent checks and 100 ps/150 mV comparison; '
              'finite supplies and 2 pF/10 kohm loads, ideal inter-macro wires; not full TX or chip qualification')
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
        SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    try:
        save()
        with (work / 'export.log').open('w') as log:
            execute([args.exporter.resolve(), work / 'export'], stdout=log, stderr=-2,
                check=True, timeout=120, memory_exclusive=False)
        report['bank'] = validate_export(work / 'export', tx_work / 'export/translator.va', bias['exported_source'])
        hashes.update({str(p): digest(p) for p in (work / 'export').iterdir() if p.is_file()})
        schematic_tx = reference(work / 'export/translator_0.va', 'psp-charge')
        for flow, translator, name, replica, bias_name in (
                ('schematic', schematic_tx, 'ReferenceTranslator', storage_reference(bias['geometry']), 'ReferenceBias'),
                ('physical', physical_tx, 'ExtractedAnalog', physical_bias, 'PhysicalBias')):
            target = work / flow
            target.mkdir()
            path = target / 'bench.cir'
            path.write_text(deck(translator, name, replica, bias_name, models / 'models',
                args.corner, args.temperature))
            hashes[str(path)] = digest(path)
            report['stage'] = 'waiting_' + flow
            save()
            if args.emit_only:
                report['flows'][flow] = dict(status='netlist_only', deck=str(path))
                continue
            with memory_slot(['loaded-mos-replica'], exclusive=not args.concurrent):
                verify_files(hashes)
                report['stage'] = flow
                save()
                with capture_reserved_trace(target / 'trace.dat', 64 * 1024 ** 2) as trace:
                    with (target / 'ngspice.log').open('w') as log:
                        execute(['prlimit', '--as=1342177280', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                            cwd=target, env=env, stdout=log, stderr=-2,
                            check=True, timeout=43200, memory_exclusive=False)
                result = measure(trace.path)
                result.update(trace_storage=trace.metadata, artifact_sha256={
                    str(trace.path): digest(trace.path), str(target / 'ngspice.log'): digest(target / 'ngspice.log')})
                report['flows'][flow] = result
                save()
                if result['status'] != 'pass':
                    raise ValueError(flow + ': loaded MOS replica absolute limits failed')
        if args.emit_only:
            verify_files(hashes)
            report.update(status='netlists_emitted', stage='complete')
            return
        report['comparison'] = [compare(a, b) for a, b in zip(
            report['flows']['schematic']['gates'], report['flows']['physical']['gates'])]
        verify_files(hashes)
        report.update(status='pass', stage='complete')
        print('PASS loaded MOS-storage replica, both circuits independently and compared')
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
    parser.add_argument('--concurrent', action='store_true', help='use a separate bounded 1.25 GiB simulation budget')
    run(parser.parse_args())
