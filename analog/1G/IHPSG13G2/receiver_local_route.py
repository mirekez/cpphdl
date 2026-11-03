#!/usr/bin/env python3
"""Reroute a limited critical-buffer ECO and measure fresh extracted timing."""
import argparse
import gc
import json
import os
from pathlib import Path
import re
import shutil
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
COMMON = ROOT / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(COMMON))
from buffer_equivalence import compare
from checks import check_timing
from receiver_path_sizing import digest, slack
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def path_buffers(text):
    names = []
    for path in text.split('Startpoint:')[1:]:
        lines = path.splitlines()
        start = next((i for i, line in enumerate(lines) if re.search(r'/Q(?:_N)? \(', line)), None)
        stop = next((i for i, line in enumerate(lines) if 'data arrival time' in line), None)
        if start is None or stop is None or stop <= start:
            continue
        for name in re.findall(r' ((?:place|wire)\d+)/X \(sg13g2_buf_(?:1|2|4|8|16)\)', '\n'.join(lines[start:stop])):
            if name not in names:
                names.append(name)
                if len(names) == 200:
                    return names
    if not names:
        raise ValueError('no explicit data-path buffer candidates')
    return names


def run(args):
    seed, work, constraints = (p.resolve() for p in (args.seed, args.output, args.constraints))
    proof = json.loads((seed / 'checks.json').read_text())
    if proof.get('status') != 'diagnostic_complete' or not proof.get('fresh_process_timing_agrees'):
        raise ValueError('independently checked receiver sizing candidate required')
    current = Path(proof.get('candidate', seed)).resolve()
    if not current.is_relative_to(seed):
        raise ValueError('candidate must belong to the seed receipt')
    hashes = dict(proof['source_sha256'])
    for name, value in proof['artifact_sha256'].items():
        if name in hashes and hashes[name] != value:
            raise ValueError('conflicting seed provenance')
        hashes[name] = value
    for p in (current / 'candidate.odb', current / 'after.v', current / 'after_paths.rpt', constraints):
        if hashes.get(str(p)) != digest(p):
            raise ValueError('unrecorded receiver candidate input: ' + str(p))
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        p = pdk / ('ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_' + corner + '.lib')
        if hashes.get(str(p)) != digest(p):
            raise ValueError('ECO must use the seed timing libraries')
    for p in (seed / 'checks.json', Path(__file__).resolve(), HERE / 'receiver_local_route.tcl',
              HERE / 'receiver_local_unbuffer.tcl', HERE / 'receiver_path_sizing.py',
              HERE / 'buffer_equivalence.py', COMMON / 'checks.py', COMMON / 'tool_process.py',
              COMMON / 'routing_rc_margin.tcl', tools / 'rcx_patterns.rules',
              tools / 'bin/openroad', tools / 'root/usr/bin/openroad',
              pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef',
              pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl'):
        value = digest(p)
        if str(p) in hashes and hashes[str(p)] != value:
            raise ValueError('ECO input changed since seed: ' + str(p))
        hashes[str(p)] = value
    names = path_buffers((current / 'after_paths.rpt').read_text())

    def verify():
        for name, value in hashes.items():
            if digest(name) != value:
                raise ValueError('ECO input changed: ' + name)

    verify()
    if shutil.disk_usage(work.parent).free < 1100 * 1024**2:
        raise ValueError('receiver route needs at least 1100 MiB free')
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', source_sha256=hashes,
                  selected_paths=names, stages=[], pid=os.getpid(),
                  timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['receiver-local-reroute'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, RECEIVER_SEED=str(current / 'candidate.odb'),
                RECEIVER_SDC=str(constraints), RECEIVER_BUFFERS=','.join(names), REPEATER_WORK=str(work),
                REPEATER_PDK=str(pdk), REPEATER_TOOLS=str(tools), RECEIVER_SOURCE=str(HERE),
                REPEATER_SOURCE=str(COMMON), OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())

            def stage(name, timeout, gib):
                report['stage'] = name
                save()
                environment['RECEIVER_ECO_STAGE'] = name
                start = time.monotonic()
                with (work / (name + '.log')).open('w') as log:
                    result = execute(['prlimit', '--as=' + str(gib * 1024**3), '--core=0', '--',
                        tools / 'bin/openroad', '-no_init', '-exit', HERE / 'receiver_local_route.tcl'],
                        env=environment, stdout=log, stderr=-2, timeout=timeout)
                report['stages'].append(dict(name=name, returncode=result.returncode,
                                            seconds=time.monotonic() - start))
                save()
                if result.returncode:
                    raise ValueError(name + ' failed; see ' + name + '.log')
                verify()

            stage('global', 3600, 4)
            report['seed_connectivity'] = compare(current / 'after.v', work / 'before.v')
            if report['seed_connectivity']['removed_buffers']:
                raise ValueError('ODB does not reproduce the seed netlist')
            report['buffer_connectivity'] = compare(work / 'before.v', work / 'unbuffered.v')
            if not report['buffer_connectivity']['removed_buffers']:
                raise ValueError('no buffer removal occurred')
            report['filler_connectivity'] = compare(work / 'unbuffered.v', work / 'unfilled.v', defer_fillers=True)
            report['placement_connectivity'] = compare(work / 'unfilled.v', work / 'global.v')
            if report['placement_connectivity']['removed_buffers']:
                raise ValueError('legalization changed signal topology')
            report['global_estimates'] = dict(setup_ns=slack(work / 'global_setup.rpt', 'max'),
                                            hold_ns=slack(work / 'global_hold.rpt', 'min'))
            save()
            gc.collect()
            stage('detail', 14400, 6)
            if (work / 'route_drc.rpt').read_text().strip():
                raise ValueError('detailed router reports violations')
            stage('extract', 1800, 4)
            # Reverse comparison permits only the restored, pinless physical
            # fillers. Functional cells and every signal endpoint must survive.
            report['routed_connectivity'] = compare(work / 'postroute.v', work / 'global.v', defer_fillers=True)
            report.update(setup_ns=slack(work / 'setup.rpt', 'max'), hold_ns=slack(work / 'hold.rpt', 'min'))
            try:
                report['timing'] = check_timing(work)
                report['status'] = 'routed_timing_candidate_pass'
            except ValueError as error:
                report.update(status='routed_timing_fail', timing_error=str(error))
            verify()
            report.update(stage='complete', artifact_sha256={str(p): digest(p)
                for p in work.iterdir() if p.is_file() and p.name != 'checks.json'})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('seed', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
