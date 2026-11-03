#!/usr/bin/env python3
"""Measure fresh OpenRCX timing after an early detailed-route iteration.

This diagnostic cannot qualify routing, timing or layout. Full routing and
all physical/functional checks remain mandatory even if estimated timing passes.
"""
import argparse
import gc
import json
import os
from pathlib import Path
import shutil
import sys
import time

from receiver_path_sizing import HERE, ROOT, digest, slack
from buffer_equivalence import compare
import receiver_allocator

COMMON = ROOT / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(COMMON))
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def screen(proof):
    if proof.get('status') != 'diagnostic_complete' or not proof.get('rounds'):
        raise ValueError('completed receiver diagnostic required')
    final = proof['rounds'][-1]
    if (final.get('name') != 'after' or final.get('connectivity', {}).get('status') != 'pass' or
            final['connectivity'].get('top') != 'PCSReceiver'):
        raise ValueError('receiver endpoint proof required')
    if not final.get('electrical_pass'):
        raise ValueError('global electrical violations must be repaired before route probing')
    for name in ('setup_ns', 'hold_ns'):
        value = final.get(name)
        if not isinstance(value, (int, float)) or not .1 <= value < 1000:
            raise ValueError('route probe requires at least 0.1 ns global setup/hold')


def run(args):
    seed, work = args.seed.resolve(), args.output.resolve()
    proof = json.loads((seed / 'checks.json').read_text())
    screen(proof)
    hashes = {str(seed / 'checks.json'): digest(seed / 'checks.json')}
    for name in ('candidate.odb', 'candidate.sdc', 'after.v'):
        path = seed / name
        if proof['artifact_sha256'].get(str(path)) != digest(path):
            raise ValueError('changed diagnostic route input: ' + name)
        hashes[str(path)] = digest(path)
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    allocator_env, allocator, allocator_files = receiver_allocator.configuration(getattr(args, 'allocator', None))
    for path in (tools / 'bin/openroad', tools / 'root/usr/bin/openroad'):
        if proof['source_sha256'].get(str(path)) != digest(path):
            raise ValueError('route probe tool differs from the diagnostic')
    paths = [Path(__file__).resolve(), HERE / 'receiver_local_route.tcl',
             HERE / 'buffer_equivalence.py', HERE / 'receiver_path_sizing.py',
             COMMON / 'tool_process.py', COMMON / 'routing_rc_margin.tcl',
             tools / 'bin/openroad', tools / 'root/usr/bin/openroad', tools / 'rcx_patterns.rules']
    lib = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        path = lib / 'lib' / f'sg13g2_stdcell_{corner}.lib'
        if proof['source_sha256'].get(str(path)) != digest(path):
            raise ValueError('route probe library differs from the diagnostic')
        paths.append(path)
    paths += [lib / 'lef/sg13g2_tech.lef', lib / 'cdl/sg13g2_stdcell.cdl']
    paths += [HERE / 'receiver_allocator.py', Path('/usr/bin/time')] + allocator_files
    hashes.update({str(p): digest(p) for p in paths})

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('route probe input changed: ' + name)

    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(),
                  source_sha256=hashes, stages=[], detail_end_iteration=1, allocator=allocator,
                  timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['receiver-route-probe'], exclusive=True) as owned:
            verify()
            if shutil.disk_usage(work).free < args.minimum_free_mib * 1024**2:
                raise ValueError('insufficient disk space for early receiver routing')
            # Hard links retain the exact immutable candidate without a second
            # large checkpoint copy. The router writes a different output DB.
            os.link(seed / 'candidate.odb', work / 'global.odb')
            os.link(seed / 'after.v', work / 'global.v')
            environment = dict(os.environ, REPEATER_WORK=str(work),
                REPEATER_PDK=str(pdk), REPEATER_TOOLS=str(tools),
                REPEATER_SOURCE=str(COMMON), RECEIVER_SDC=str(seed / 'candidate.sdc'),
                RECEIVER_ROUTE_PROBE='1', OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            environment = receiver_allocator.environment(environment, allocator_env)
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            for stage, timeout, gib in [('detail', 14400, 6), ('extract', 1800, 4)]:
                report['stage'] = stage
                save()
                environment['RECEIVER_ECO_STAGE'] = stage
                started = time.monotonic()
                with (work / (stage + '.log')).open('w') as log:
                    result = execute(['/usr/bin/time', '-v', '-o', work / (stage + '.resources'),
                        'prlimit', '--as=' + str(gib * 1024**3), '--core=0', '--',
                        tools / 'bin/openroad', '-no_init', '-exit', HERE / 'receiver_local_route.tcl'],
                        env=environment, stdout=log, stderr=-2, timeout=timeout)
                report['stages'].append(dict(name=stage, returncode=result.returncode,
                                            seconds=time.monotonic() - started))
                if result.returncode:
                    raise ValueError('route probe ' + stage + ' failed; inspect its log')
                verify()
            report['connectivity'] = compare(work / 'postroute.v', work / 'global.v', defer_fillers=True)
            gc.collect()
            report.update(setup_ns=slack(work / 'setup.rpt', 'max'),
                          hold_ns=slack(work / 'hold.rpt', 'min'),
                          electrical_pass='VIOLATED' not in (work / 'electrical.rpt').read_text(),
                          router_report_empty=not (work / 'route_drc.rpt').read_text().strip(),
                          status='early_route_diagnostic_complete', stage='complete')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir()
                                    if p.is_file() and p.name not in ('checks.json', 'checks.tmp')}
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('seed', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--minimum-free-mib', type=int, default=2000)
    parser.add_argument('--allocator', type=Path, help='explicit local libjemalloc.so.2 for routing')
    run(parser.parse_args())
