#!/usr/bin/env python3
"""Verify retained routed frame-buffer geometry with the repaired SRAM reference."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
COMMON = ROOT / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(COMMON))
from tool_process import SLOT_OWNER, configure_cleanup, memory_slot

parser = argparse.ArgumentParser(description=__doc__)
for name in ('geometry', 'routed', 'sram-lvs', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
parser.add_argument('--tools', type=Path, default=ROOT / 'out/klayout-lvs-0.30.12')
args = parser.parse_args()
configure_cleanup()
work = args.output.resolve()
work.mkdir(parents=True, exist_ok=False)
dependency = args.sram_lvs.resolve()
receipt = work / 'checks.json'
report = dict(status='running', stage='waiting_for_sram_regeneration', pid=os.getpid(),
              full_chip_qualified=False, stages=[])


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save():
    temporary = receipt.with_suffix('.tmp')
    temporary.write_text(json.dumps(report, indent=2) + '\n')
    temporary.replace(receipt)


def available():
    return next(int(s.split()[1]) * 1024 for s in Path('/proc/meminfo').read_text().splitlines()
                if s.startswith('MemAvailable:'))


def verify(hashes):
    for name, expected in hashes.items():
        if digest(name) != expected:
            raise ValueError('embedded SRAM input changed: ' + name)


save()
try:
    sources = [Path(__file__).resolve(), HERE / 'flow_control_sram_lvs.py', HERE / 'embedded_sram_lvs.py',
               HERE / 'sram_library.py', HERE / 'sram_lvs_compare.py', HERE / 'sram_lvs_audit.py',
               HERE / 'sram_lvs_alignment.rb', COMMON / 'checks.py', COMMON / 'tool_process.py']
    hashes = {str(p): digest(p) for p in sources}
    report['source_sha256'] = hashes
    save()
    deadline = time.monotonic() + 86400
    while True:
        result = json.loads(dependency.read_text())
        if result['status'] == 'filled_graph_correspondence_pass':
            break
        if result['status'] != 'running':
            raise ValueError('SRAM regeneration did not pass: ' + str(result.get('error')))
        if time.monotonic() > deadline:
            raise TimeoutError('SRAM regeneration dependency did not finish')
        time.sleep(5)
    verify(hashes)
    report['stage'] = 'waiting_for_memory_slot'
    save()
    with memory_slot(['embedded-sram-full-geometry'], exclusive=True) as owned:
        report['stage'] = 'waiting_for_available_memory'
        save()
        deadline = time.monotonic() + 43200
        while available() < 3 * 1024**3:
            if time.monotonic() > deadline:
                raise TimeoutError('insufficient memory for embedded SRAM verification')
            time.sleep(2)
        pdk, tools = args.pdk.resolve(), args.tools.resolve()
        python = pdk / '.cpphdl/venv/bin/python'
        environment = dict(os.environ, OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2',
                           PYTHONDONTWRITEBYTECODE='1', PYTHONPATH=str(tools / 'python'),
                           PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'])
        if owned:
            environment[SLOT_OWNER] = str(os.getpid())

        def stage(name, command, timeout, limit):
            report['stage'] = name
            save()
            started = time.monotonic()
            with (work / (name + '.log')).open('w') as log:
                child = subprocess.Popen(['prlimit', '--as=' + str(limit), '--', *map(str, command)],
                                         cwd=ROOT, env=environment, stdout=log, stderr=-2, start_new_session=True)
                try:
                    while child.poll() is None:
                        if available() < 512 * 1024**2:
                            raise MemoryError('embedded SRAM memory reserve reached')
                        if time.monotonic() - started > timeout:
                            raise TimeoutError(name + ' exceeded bounded runtime')
                        time.sleep(.5)
                except BaseException:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
                    raise
            report['stages'].append(dict(name=name, returncode=child.returncode,
                                          seconds=time.monotonic() - started))
            save()
            if child.returncode:
                raise ValueError(name + ' failed; see ' + str(work / (name + '.log')))
            verify(hashes)

        fixture = work / 'fixture'
        stage('preparation', [python, HERE / 'flow_control_sram_lvs.py', 'prepare',
                             '--geometry', args.geometry.resolve(),
                             '--routed', args.routed.resolve(),
                             '--sram-lvs', dependency, '--pdk', pdk, '--output', fixture], 600, 2 * 1024**3)
        prepared = json.loads((fixture / 'preparation.json').read_text())
        hashes.update(prepared['source_sha256'])
        hashes.update(prepared['artifact_sha256'])
        hashes.update({str(p): digest(p) for folder in ('root', 'python', 'bin')
                       for p in (tools / folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts})
        report['source_sha256'] = hashes
        save()
        deck = fixture / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs'
        stage('extraction', [python, deck / 'run_lvs.py', '--layout', fixture / 'assembly.gds',
                             '--netlist', fixture / 'assembly.cir', '--topcell', 'FlowControlledRepeater',
                             '--top_lvl_pins', '--run_mode', 'deep', '--run_dir', work / 'lvs'],
              # The retained eight-bank extraction took 3313 seconds. A
              # single-macro timeout cannot cover its full-chip geometry.
              7200, 4 * 1024**3)
        database = work / 'lvs/assembly.lvsdb'
        extraction = work / 'extraction_receipt.json'
        extraction.write_text(json.dumps(dict(source_sha256=hashes,
            artifact_sha256={str(database): digest(database)}), indent=2) + '\n')
        stage('correspondence', [python, HERE / 'flow_control_sram_lvs.py', 'qualify', '--fixture', fixture,
                                 '--source', database, '--extraction-receipt', extraction,
                                 '--output', work / 'correspondence'], 600, 2 * 1024**3)
        compared = json.loads((work / 'correspondence/checks.json').read_text())
        if compared['status'] != 'assembly_graph_correspondence_pass':
            raise ValueError('complete assembly graph audit did not pass')
        drc = pdk / 'ihp-sg13g2/libs.tech/klayout/tech/drc'
        stage('drc', [python, drc / 'run_drc.py', '--path', fixture / 'assembly.gds',
                      '--topcell', 'FlowControlledRepeater', '--run_dir', work / 'drc', '--mp', '1',
                      '--density_thr', '1', '--antenna'], 7200, 4 * 1024**3)
        from checks import check_drc
        report['drc'] = check_drc(work / 'drc/assembly_FlowControlledRepeater_full.lyrdb', drc)
        verify(hashes)
        report.update(status='assembly_geometry_pass', stage='complete',
                      assembly_audit=compared['assembly_audit'],
                      artifact_sha256={str(fixture / 'assembly.gds'): digest(fixture / 'assembly.gds')})
except BaseException as error:
    report.update(status='fail', error=str(error))
    raise
finally:
    save()
