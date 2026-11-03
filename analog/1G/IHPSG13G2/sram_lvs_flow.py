#!/usr/bin/env python3
"""Regenerate the SRAM extraction and verify its complete native graph correspondence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import SLOT_OWNER, configure_cleanup, memory_slot


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(hashes):
    for name, expected in hashes.items():
        if digest(name) != expected:
            raise ValueError('input changed: ' + name)


def available():
    return next(int(line.split()[1]) * 1024 for line in Path('/proc/meminfo').read_text().splitlines()
                if line.startswith('MemAvailable:'))


def run(args):
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    python = pdk / '.cpphdl/venv/bin/python'
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(),
                  full_sram_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.json.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['sram-lvs-flow'], exclusive=True) as owned:
            deadline = time.monotonic() + 43200
            while available() < 3 * 1024**3:
                if time.monotonic() > deadline:
                    raise TimeoutError('insufficient memory for SRAM extraction')
                time.sleep(2)
            environment = dict(os.environ, OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2',
                               PYTHONDONTWRITEBYTECODE='1', PYTHONPATH=str(tools / 'python'),
                               PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'])
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())

            def stage(name, command, virtual_limit, timeout):
                report['stage'] = name
                save()
                started = time.monotonic()
                with (work / (name + '.log')).open('w') as log:
                    child = subprocess.Popen(['prlimit', '--as=' + str(virtual_limit), '--', *map(str, command)],
                                             cwd=ROOT, env=environment, stdout=log, stderr=-2, start_new_session=True)
                    try:
                        while child.poll() is None:
                            if available() < 512 * 1024**2:
                                raise MemoryError('SRAM flow available-memory guard')
                            if time.monotonic() - started > timeout:
                                raise TimeoutError(name + ' exceeded bounded runtime')
                            time.sleep(.5)
                    except BaseException:
                        os.killpg(child.pid, signal.SIGKILL)
                        child.wait()
                        raise
                report[name] = dict(returncode=child.returncode, seconds=time.monotonic() - started)
                save()
                if child.returncode:
                    raise ValueError(name + ' failed; see ' + str(work / (name + '.log')))

            stage('tools', [python, HERE / 'prepare_sram_lvs_tools.py', '--output', tools,
                            '--python', python, '--verify-only'], 512 * 1024**2, 60)
            fixture = work / 'fixture'
            stage('preparation', [python, HERE / 'prepare_sram_lvs.py', '--pdk', pdk, '--output', fixture,
                                  *(['--fill'] if args.fill else [])], 4 * 1024**3, 600)
            preparation = json.loads((fixture / 'preparation.json').read_text())
            inputs = {**preparation['source_sha256'], **preparation['artifact_sha256']}
            deck = fixture / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs'
            additional = [Path(__file__).resolve(), HERE / 'qualify_sram_lvs.py', HERE / 'sram_lvs_compare.py',
                          HERE / 'sram_lvs_audit.py', HERE / 'prepare_sram_lvs_tools.py',
                          *[p for p in deck.rglob('*') if p.is_file()],
                          *[p for folder in ('root', 'python', 'bin') for p in (tools / folder).rglob('*')
                            if p.is_file() and '__pycache__' not in p.parts]]
            inputs.update({str(p.resolve()): digest(p) for p in additional})
            verify(inputs)
            report['source_sha256'] = inputs
            save()
            stage('extraction', [python, deck / 'run_lvs.py', '--layout', fixture / 'macro.gds',
                                 '--netlist', fixture / 'reference.cir', '--topcell',
                                 'RM_IHPSG13_2P_1024x16_c2_bm_bist', '--top_lvl_pins', '--run_mode', 'deep',
                                 '--run_dir', work / 'lvs'], 4 * 1024**3, 600)
            native = work / 'lvs/macro.lvsdb'
            if not native.is_file():
                raise ValueError('extraction did not save its native database')
            verify(inputs)
            receipt = work / 'extraction_receipt.json'
            receipt.write_text(json.dumps(dict(source_sha256=inputs, artifact_sha256={str(native): digest(native)},
                                               scope='completed extraction; initial correspondence is provisional'), indent=2) + '\n')
            stage('correspondence', [python, HERE / 'qualify_sram_lvs.py', '--source', native,
                                    '--reference', fixture / 'reference.cir', '--extraction-receipt', receipt,
                                    '--resistor-deck', deck / 'rule_decks/res_extraction.lvs', '--expand-column-logic',
                                    '--output', work / 'correspondence'], 1024**3, 300)
            result = json.loads((work / 'correspondence/checks.json').read_text())
            if result['status'] != 'graph_correspondence_pass':
                raise ValueError('incomplete independent correspondence audit')
            verify(inputs)
            report.update(status='filled_graph_correspondence_pass' if args.fill else 'graph_correspondence_pass',
                          stage='complete', independent_audit=result['independent_audit'],
                          correspondence_receipt=str(work / 'correspondence/checks.json'))
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/klayout-lvs-0.30.12')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fill', action='store_true')
    run(parser.parse_args())
