#!/usr/bin/env python3
"""Run full DRC and a fresh transistor extraction after the row-end well repair."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from checks import check_drc
from flow_control_sram_lvs import combine_hashes, digest, verify
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    candidate, retained, comparison, work = (p.resolve() for p in
        (args.candidate, args.retained, args.comparison, args.output))
    repair = json.loads((candidate / 'checks.json').read_text())
    original = json.loads(comparison.read_text())
    old_fixture = retained / 'fixture'
    prepared = json.loads((old_fixture / 'preparation.json').read_text())
    if repair.get('status') != 'candidate_generated' or repair.get('modified_layers') != [[31, 0]]:
        raise ValueError('verified row-end well candidate required')
    if original.get('status') != 'assembly_graph_correspondence_pass':
        raise ValueError('complete original assembly graph comparison required')
    source = old_fixture / 'assembly.gds'
    if repair['source_sha256'].get(str(source)) != digest(source):
        raise ValueError('well candidate belongs to another original assembly')
    if original['source_sha256'].get(str(source)) != digest(source):
        raise ValueError('original LVS belongs to another assembly')
    hashes = combine_hashes(original['source_sha256'], original['artifact_sha256'],
                            repair['source_sha256'], repair['artifact_sha256'])
    for p in (comparison, candidate / 'checks.json', Path(__file__).resolve(),
              HERE / 'resume_embedded_sram.py', HERE / 'embedded_sram_ports.py',
              HERE / 'embedded_sram_bootstrap.py', HERE / 'lvs_comparison_reset.py',
              ROOT / 'pdk/1G_repeater_IHPSG13G2/checks.py',
              ROOT / 'pdk/1G_repeater_IHPSG13G2/tool_process.py'):
        hashes = combine_hashes(hashes, {str(p): digest(p)})
    drc = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech/drc'
    for directory in (drc, *(args.tools.resolve() / name for name in ('bin', 'root', 'python'))):
        for p in directory.rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts:
                hashes = combine_hashes(hashes, {str(p): digest(p)})
    verify(hashes)
    if shutil.disk_usage(work.parent).free < 1200 * 1024**2:
        raise ValueError('fresh extraction requires at least 1200 MiB free')
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', stages=[], pid=os.getpid(),
                  source_sha256=hashes, full_chip_qualified=False, full_sram_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['row-end-well-full-qualification'], exclusive=True) as owned:
            verify(hashes)
            producer = work / 'producer'
            fixture = producer / 'fixture'
            fixture.mkdir(parents=True)
            shutil.copyfile(candidate / 'assembly.gds', fixture / 'assembly.gds')
            shutil.copyfile(old_fixture / 'core.cdl', fixture / 'core.cdl')
            shutil.copytree(old_fixture / 'pdk', fixture / 'pdk')
            standard = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl'
            (fixture / 'assembly.cir').write_text('\n'.join('.include "' + str(p) + '"'
                for p in (standard, prepared['sram_reference'], fixture / 'core.cdl')) + '\n')
            artifacts = {str(p): digest(p) for p in fixture.rglob('*') if p.is_file()}
            # Preservation is relative to the repaired input GDS. The repair
            # itself changes NWell and therefore needs this new extraction.
            preparation = dict(status='prepared', preserved_nontext_geometry=True,
                copied_geometry_source=str(candidate / 'assembly.gds'),
                physical_repair_receipt=str(candidate / 'checks.json'),
                source_sha256=hashes, artifact_sha256=artifacts,
                sram_reference=prepared['sram_reference'])
            (fixture / 'preparation.json').write_text(json.dumps(preparation, indent=2) + '\n')
            hashes = combine_hashes(hashes, artifacts,
                {str(fixture / 'preparation.json'): digest(fixture / 'preparation.json')})
            report['source_sha256'] = hashes
            environment = dict(os.environ, OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2',
                PYTHONDONTWRITEBYTECODE='1', PYTHONPATH=str(args.tools.resolve() / 'python'),
                PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            python = args.pdk.resolve() / '.cpphdl/venv/bin/python'

            def stage(name, command, timeout):
                report['stage'] = name
                save()
                start = time.monotonic()
                with (work / (name + '.log')).open('w') as log:
                    child = execute(['prlimit', '--as=4294967296', '--core=0', '--', *command],
                                    env=environment, stdout=log, stderr=-2, timeout=timeout)
                report['stages'].append(dict(name=name, returncode=child.returncode,
                                            seconds=time.monotonic() - start))
                save()
                if child.returncode:
                    raise ValueError(name + ' failed; see ' + name + '.log')
                verify(hashes)

            stage('drc', [python, drc / 'run_drc.py', '--path', fixture / 'assembly.gds',
                          '--topcell', 'FlowControlledRepeater', '--run_dir', work / 'drc',
                          '--mp', '1', '--density_thr', '1', '--antenna'], 7200)
            report['drc'] = check_drc(work / 'drc/assembly_FlowControlledRepeater_full.lyrdb', drc)
            deck = fixture / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs'
            stage('extraction', [python, deck / 'run_lvs.py', '--layout', fixture / 'assembly.gds',
                '--netlist', fixture / 'assembly.cir', '--topcell', 'FlowControlledRepeater',
                '--top_lvl_pins', '--run_mode', 'deep', '--run_dir', producer / 'lvs'], 7200)
            database = producer / 'lvs/assembly.lvsdb'
            extraction = dict(source_sha256=hashes, artifact_sha256={str(database): digest(database)})
            (producer / 'extraction_receipt.json').write_text(json.dumps(extraction, indent=2) + '\n')
            # Freeze the producer receipt: the independent comparer binds its
            # hash while the overall qualification receipt continues to evolve.
            (producer / 'checks.json').write_text(json.dumps(dict(status='extracted',
                stages=report['stages'], source_sha256=hashes), indent=2) + '\n')
            stage('correspondence', [python, HERE / 'resume_embedded_sram.py', '--retained', producer,
                                   '--output', work / 'correspondence'], 1200)
            proof = json.loads((work / 'correspondence/checks.json').read_text())
            if proof.get('status') != 'assembly_graph_correspondence_pass':
                raise ValueError('complete repaired assembly graph comparison failed')
            report.update(status='assembly_geometry_pass', stage='complete',
                          assembly_audit=proof['assembly_audit'],
                          artifact_sha256={str(fixture / 'assembly.gds'): digest(fixture / 'assembly.gds'),
                              str(work / 'drc/assembly_FlowControlledRepeater_full.lyrdb'):
                              digest(work / 'drc/assembly_FlowControlledRepeater_full.lyrdb')})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'retained', 'comparison', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/klayout-lvs-0.30.12')
    run(parser.parse_args())
