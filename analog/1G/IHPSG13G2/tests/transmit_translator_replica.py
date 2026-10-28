"""Check one actual translator and shared replica through startup and switching."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(ROOT / '1G/IHPSG13G2/tests'), str(ROOT / '1G/IHPSG13G2'),
               str(ROOT / 'VerilogA'), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_translator_extracted import bench, reference, compare, contracts
from transmit_translation_bias import reference as bias_reference
from resistor_body_extraction import adapt
from extract_analog import digest
from trace_storage import archive_trace
from tool_process import execute, configure_cleanup


def measure(directory):
    rows = [[float(v) for v in line.split()] for line in (directory / 'trace.dat').read_text().splitlines()[1:]]
    if (not rows or len(rows[0]) < 8 or not 0 <= rows[0][0] <= 1e-12 or
            any(len(r) != len(rows[0]) or not all(math.isfinite(v) for v in r) for r in rows) or
            any(not 0 < b[0] - a[0] <= 31.26e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete connected startup trace')
    result = contracts([r[:7] for r in rows], 1.2, 3.3, True, startup=2e-6)
    result['maximum_lv_gate_difference_V'] = max(abs(v) for r in rows for v in r[7:])
    result['checks']['lv_gate_exposure'] = result['maximum_lv_gate_difference_V'] <= 1.65
    bias_rows = [[float(v) for v in line.split()] for line in (directory / 'bias.dat').read_text().splitlines()[1:]]
    if (len(bias_rows) != len(rows) or any(len(row) != 2 for row in bias_rows) or
            any(a[0] != b[0] or not math.isfinite(b[1]) for a, b in zip(rows, bias_rows))):
        raise ValueError('missing connected bias observations')
    settled = [r[1] for r in bias_rows if r[0] >= 1.6e-6]
    result['bias_range_V'] = [min(settled), max(settled)]
    result['checks']['bias_headroom'] = 1.2 < min(settled) <= max(settled) < 2.3
    result['checks']['bounded_bias_startup'] = all(-.1 < r[1] < 3.45 for r in bias_rows)
    result['status'] = 'pass' if all(result['checks'].values()) else 'fail'
    return result


def merge_hashes(destination, incoming):
    # A directly used helper may also belong to an upstream proof. Its current
    # hash must never overwrite a differing recorded hash and hide stale proof.
    for path, value in incoming.items():
        if path in destination and destination[path] != value:
            raise ValueError('inconsistent integration evidence: ' + path)
        destination[path] = value


def run(args):
    proofs = [json.loads(path.read_text()) for path in (args.translator, args.replica)]
    hashes = {}
    for proof in proofs:
        if proof.get('status') != 'pass':
            raise ValueError('requires passing local geometry proofs')
        for group in ('source_sha256', 'artifact_sha256'):
            merge_hashes(hashes, proof[group])
    if not proofs[0].get('missing_device_rejected') or not proofs[1].get('missing_replica_rejected'):
        raise ValueError('both component fault-rejection proofs are required')
    sources = [Path(__file__).resolve(), args.translator, args.replica, args.ngspice,
               ROOT / '1G/IHPSG13G2/tests/transmit_translator_extracted.py',
               ROOT / '1G/IHPSG13G2/tests/transmit_translation_bias.py',
               ROOT / '1G/tests/level_shifter.py', ROOT / 'VerilogA/trace_storage.py']
    merge_hashes(hashes, {str(p.resolve()): digest(p) for p in sources})
    if any(digest(path) != value for path, value in hashes.items()):
        raise ValueError('stale integration inputs')
    tx, bias = proofs
    tx_work, bias_work = (Path(p['work']) for p in proofs)
    physical_bias, partition = adapt((bias_work / 'extraction/named/capacitance.spice').read_text(),
        (bias_work / 'extraction/conserved.spice').read_text(),
        (bias_work / 'extraction/named/ExtractedAnalog.ext').read_text(), ['VDD', 'VSS', 'VLOGIC', 'BIAS'])
    physical_bias = physical_bias.replace('ExtractedAnalog', 'PhysicalBias')
    physical_tx = (tx_work / 'extraction/conserved.spice').read_text()
    reference_tx = reference(tx_work / 'export/translator.va', tx['geometry']['process_realization']['storage'])
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, flows={},
        corner=args.corner, temperature_C=args.temperature, load_F=2e-12,
        startup_s=2e-6, observation_s=128e-9, replica_body_partition=partition,
        scope='one physical translator connected to actual shared 50 pF replica; '
              'ideal macro-to-macro wiring; not the 22-translator bank or a routed TX proof')
    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
               SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')
    try:
        save()
        for flow, tx_model, tx_name, bias_model, bias_name in (
                ('schematic', reference_tx, 'ReferenceTranslator', bias_reference(bias['geometry']), 'ReferenceBias'),
                ('physical', physical_tx, 'ExtractedAnalog', physical_bias, 'PhysicalBias')):
            directory = work / flow
            directory.mkdir()
            text = bench(tx_model, tx_name, models, 2e-12, args.corner, args.temperature)
            old = 'VBIAS_SOURCE BSOURCE 0 1.8\nRBIAS BSOURCE BIAS 100\nCBIAS BIAS 0 1p\n'
            if text.count(old) != 1:
                raise ValueError('translator test bias topology changed')
            text = text.replace(old, bias_model + f'Xbias VDD 0 VLOGIC BIAS {bias_name}\n')
            text = text.replace('.temp ', f'.lib "{models / "models/cornerRES.lib"}" res_typ\n.temp ', 1)
            match = re.search(r'VCMD COMMAND 0 PWL\(([^)]*)\)', text)
            fields = match[1].split()
            points = [(float(t), float(v)) for t, v in zip(fields[::2], fields[1::2])]
            points = [(t + (2e-6 - 32e-9) if t else 0, v) for t, v in points]
            text = text[:match.start(1)] + ' '.join(f'{t:.17g} {v}' for t, v in points) + text[match.end(1):]
            text = text.replace('tran 7.8125p 160n 0 7.8125p uic', 'tran 31.25p 2128n 0 31.25p uic')
            text = text.replace('.save ', '.save v(BIAS) ', 1)
            text = text.replace('\nquit\n', '\nwrdata bias.dat v(BIAS)\nquit\n')
            text = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', text, flags=re.M)
            deck = directory / 'bench.cir'
            deck.write_text(text)
            hashes[str(deck)] = digest(deck)
            if args.emit_only:
                report['flows'][flow] = dict(status='netlist_only', deck=str(deck), sha256=digest(deck))
                save()
                continue
            with (directory / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=1073741824', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                    cwd=directory, env=env, stdout=log, stderr=-2, check=True, timeout=7200, memory_exclusive=False)
            result = measure(directory)
            result['artifact_sha256'] = {str(deck): hashes[str(deck)]}
            result['trace_storage'] = [archive_trace(directory / n)[1] for n in ('trace.dat', 'bias.dat')]
            report['flows'][flow] = result
            save()
            print(flow, result['status'], result['bias_range_V'], result['checks'], flush=True)
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('integration inputs changed')
        if args.emit_only:
            report['status'] = 'netlists_emitted'
            return
        if any(result['status'] != 'pass' for result in report['flows'].values()):
            raise ValueError('connected translator absolute checks failed')
        report['comparison'] = compare(report['flows']['schematic'], report['flows']['physical'])
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()

if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('translator', 'replica', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--emit-only', action='store_true', help='write netlists without an electrical verdict')
    run(parser.parse_args())
