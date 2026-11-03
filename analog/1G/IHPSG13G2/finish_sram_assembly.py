#!/usr/bin/env python3
"""Run complete DRC on the retained assembly after successful full-graph LVS.

This completes the SRAM/control geometry check, not whole-ASIC signoff or
SRAM electrical characterization. Original extraction/geometry are retained.
"""
import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from checks import check_drc
from sram_library import digest
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    retained, comparison, work = args.retained.resolve(), args.comparison.resolve(), args.output.resolve()
    proof = json.loads(comparison.read_text())
    if (proof.get('status') != 'assembly_graph_correspondence_pass' or proof.get('stage') != 'complete'
            or proof.get('assembly_audit', {}).get('status') != 'pass'):
        raise ValueError('completed independent assembly graph audit required')
    extraction = retained / 'extraction_receipt.json'
    fixture = retained / 'fixture'
    if proof['source_sha256'].get(str(extraction)) != digest(extraction):
        raise ValueError('assembly comparison belongs to another extraction')
    hashes = dict(proof['source_sha256'])
    for path, expected in proof['artifact_sha256'].items():
        if path in hashes and hashes[path] != expected:
            raise ValueError('conflicting assembly artifact provenance')
        hashes[path] = expected

    def record(path):
        name, value = str(path), digest(path)
        if name in hashes and hashes[name] != value:
            raise ValueError('qualification input changed since LVS: ' + name)
        hashes[name] = value

    for path in (comparison, Path(__file__).resolve(), ROOT / 'pdk/1G_repeater_IHPSG13G2/checks.py',
                 ROOT / 'pdk/1G_repeater_IHPSG13G2/tool_process.py'):
        record(path)
    drc = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech/drc'
    for directory in (drc, *(args.tools.resolve() / name for name in ('bin', 'root', 'python'))):
        for path in directory.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts:
                record(path)
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(),
                  source_sha256=hashes, full_chip_qualified=False, full_sram_qualified=False)

    def save():
        tmp = work / 'checks.tmp'
        tmp.write_text(json.dumps(report, indent=2) + '\n')
        tmp.replace(work / 'checks.json')

    def verify():
        for path, expected in hashes.items():
            if digest(path) != expected:
                raise ValueError('assembly qualification input changed: ' + path)

    save()
    try:
        with memory_slot(['sram-assembly-complete-drc'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2',
                               PYTHONDONTWRITEBYTECODE='1', PYTHONPATH=str(args.tools.resolve() / 'python'),
                               PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            report['stage'] = 'full_drc'
            save()
            with (work / 'drc.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                                  args.pdk.resolve() / '.cpphdl/venv/bin/python', drc / 'run_drc.py',
                                  '--path', fixture / 'assembly.gds', '--topcell', 'FlowControlledRepeater',
                                  '--run_dir', work / 'drc', '--mp', '1', '--density_thr', '1', '--antenna'],
                                 env=environment, stdout=log, stderr=-2, timeout=7200)
            report['drc_exit_code'] = result.returncode
            if result.returncode:
                raise ValueError('full assembly DRC failed; see drc.log')
            report['drc'] = check_drc(work / 'drc/assembly_FlowControlledRepeater_full.lyrdb', drc)
            verify()
            report.update(status='assembly_geometry_pass', stage='complete',
                          assembly_audit=proof['assembly_audit'],
                          artifact_sha256={str(p): digest(p) for p in
                                           (fixture / 'assembly.gds', Path(report['drc']['report']))})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('retained', 'comparison', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/klayout-lvs-0.30.12')
    run(parser.parse_args())
