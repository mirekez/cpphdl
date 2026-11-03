#!/usr/bin/env python3
"""Resume a recorded SRAM extraction by rebuilding only its comparison data.

Run with the same isolated KLayout Python runtime as the extraction. Original
receipts and database are immutable; all new evidence goes to a new directory.
This verifies graph correspondence, not timing, DRC or full-chip qualification.
"""
import argparse
import json
import os
from pathlib import Path
import sys

import klayout.db as db

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import configure_cleanup, execute, memory_slot
from flow_control_sram_lvs import MACRO, TOP, combine_hashes, digest, ports, verify
from embedded_sram_bootstrap import BootstrappedSramCorrespondence
from embedded_sram_ports import output_labels, restore_unused_outputs
from lvs_comparison_reset import reset_comparison
from sram_lvs_audit import sheet_resistance


def run(args):
    retained, work = args.retained.resolve(), args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(),
                  full_chip_qualified=False, full_sram_qualified=False)

    def save():
        path = work / 'checks.tmp'
        path.write_text(json.dumps(report, indent=2) + '\n')
        path.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['embedded-sram-comparison-resume'], exclusive=True):
            fixture = retained / 'fixture'
            extraction_path = retained / 'extraction_receipt.json'
            extraction = json.loads(extraction_path.read_text())
            preparation = fixture / 'preparation.json'
            prepared = json.loads(preparation.read_text())
            flow = json.loads((retained / 'checks.json').read_text())
            if not any(s['name'] == 'extraction' and s['returncode'] == 0 for s in flow['stages']):
                raise ValueError('completed extraction required')
            if prepared.get('status') != 'prepared' or not prepared.get('preserved_nontext_geometry'):
                raise ValueError('geometry-preserving preparation required')
            if any(extraction['source_sha256'].get(p) != h for p, h in prepared['artifact_sha256'].items()):
                raise ValueError('extraction not bound to prepared assembly')
            hashes = combine_hashes(prepared['source_sha256'], prepared['artifact_sha256'],
                                    extraction['source_sha256'], extraction['artifact_sha256'])
            verify(hashes)
            source = retained / 'lvs/assembly.lvsdb'
            if extraction['artifact_sha256'].get(str(source)) != digest(source):
                raise ValueError('native extraction lacks producer provenance')
            for p in (Path(__file__).resolve(), HERE / 'lvs_comparison_reset.py', HERE / 'embedded_sram_bootstrap.py',
                      HERE / 'embedded_sram_ports.py',
                      extraction_path, preparation, retained / 'checks.json'):
                hashes[str(p)] = digest(p)
            report.update(source_sha256=hashes, klayout_version=db.__version__, stage='reset_comparison')
            save()
            reset = work / 'uncompared.lvsdb'
            report['reset'] = reset_comparison(source, reset)
            if report['reset']['source_sha256'] != extraction['artifact_sha256'][str(source)]:
                raise ValueError('native extraction changed while reading')
            database = db.LayoutVsSchematic()
            database.read(str(reset))
            report['unused_output_ports'] = restore_unused_outputs(
                database, MACRO, output_labels(fixture / 'assembly.gds', MACRO))
            comparer = BootstrappedSramCorrespondence(database, MACRO, TOP)
            # Align the known column-logic boundaries before asking KLayout to
            # compare them. Native 0.30.12 can crash while sorting the mismatch
            # xref for the unaligned large SRAM. Expansion checks both exact
            # inventories and recursively instantiated device conservation.
            report['boundary_expansion'] = comparer.expand_column_logic()
            report['stage'] = 'rebuilding_leaf_correspondence'
            save()
            report['leaf_seed'] = comparer.seed_leaves()
            report['stage'] = 'complete_graph_audit'
            save()
            sheet = sheet_resistance(fixture / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs/rule_decks/res_extraction.lvs')
            report.update(comparer.run_assembly(ports(prepared['sram_reference'], MACRO),
                                               ports(fixture / 'core.cdl', TOP), sheet))
            output = work / 'assembly.lvsdb'
            database.write(str(output), True)
            verify(hashes)
            report.update(stage='complete', artifact_sha256={str(output): digest(output)})
            # The reset database is reproducible and duplicates hundreds of MB.
            reset.unlink()
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retained', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--worker', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.worker:
        run(args)
    else:
        if args.output.exists():
            parser.error('output directory must be new')
        # Native aborts cannot execute the worker's finally block. Record them
        # in the parent so a failed comparison never remains marked running.
        code, error = 1, None
        try:
            result = execute(['prlimit', '--as=3221225472', '--core=0', '--',
                              sys.executable, Path(__file__).resolve(), *sys.argv[1:], '--worker'],
                             timeout=1200, memory_exclusive=False)
            code = result.returncode
        except BaseException as failure:
            error = str(failure)
        if code:
            receipt = args.output / 'checks.json'
            if receipt.exists():
                failed = json.loads(receipt.read_text())
                failed.update(status='fail', worker_exit_code=code,
                              error=error or failed.get('error') or 'native comparison worker terminated')
                receipt.write_text(json.dumps(failed, indent=2) + '\n')
        raise SystemExit(0 if code == 0 else 1)
