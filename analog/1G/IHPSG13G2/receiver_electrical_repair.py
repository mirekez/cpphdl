#!/usr/bin/env python3
"""Repair extracted receiver electrical violations and prove the resulting topology.

The output is a global-route candidate; detailed routing and new RC extraction
remain mandatory. Never qualify timing using the old SPEF after buffer insertion.
"""
import argparse
import gc
import json
import os
from pathlib import Path
import sys
import time

from receiver_path_sizing import HERE, ROOT, digest, slack
from buffer_equivalence import compare as buffer_compare
from sizing_equivalence import compare as sizing_compare
from repair_equivalence import compare as repair_compare
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    seed, routed, work = (p.resolve() for p in (args.seed, args.routed, args.output))
    proof = json.loads((seed / 'checks.json').read_text())
    if proof.get('status') != 'diagnostic_complete' or not proof.get('fresh_process_timing_agrees'):
        raise ValueError('completed independently reloaded sizing seed required')
    candidate = Path(proof['candidate']).resolve()
    if not candidate.is_relative_to(seed):
        raise ValueError('candidate lies outside the verified sizing run')
    hashes = dict(proof['source_sha256'])
    for name, expected in proof['artifact_sha256'].items():
        if name in hashes and hashes[name] != expected:
            raise ValueError('conflicting sizing provenance')
        hashes[name] = expected
    for p in (candidate / 'candidate.odb', candidate / 'after.v', routed / 'core.spef',
              routed / 'cells.v', args.constraints.resolve(),
              args.tools.resolve() / 'bin/openroad', args.tools.resolve() / 'root/usr/bin/openroad'):
        if hashes.get(str(p)) != digest(p):
            raise ValueError('electrical repair input differs from the sizing proof: ' + str(p))
    for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        p = args.pdk.resolve() / ('ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_' + corner + '.lib')
        if hashes.get(str(p)) != digest(p):
            raise ValueError('electrical repair timing library changed')
    for p in (Path(__file__).resolve(), HERE / 'receiver_electrical_repair.tcl',
              HERE / 'repair_equivalence.py', seed / 'checks.json'):
        hashes[str(p)] = digest(p)

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('electrical repair source changed: ' + name)

    verify()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(), stages=[],
                  source_sha256=hashes, timing_qualified=False, physical_qualified=False,
                  full_chip_qualified=False)
    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')
    save()
    try:
        with memory_slot(['receiver-electrical-repair'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, REPEATER_WORK=str(work),
                REPEATER_PDK=str(args.pdk.resolve()), RECEIVER_SEED=str(candidate / 'candidate.odb'),
                RECEIVER_SPEF=str(routed / 'core.spef'), RECEIVER_SDC=str(args.constraints.resolve()),
                OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            report['stage'] = 'global'
            save()
            started = time.monotonic()
            with (work / 'global.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                    args.tools.resolve() / 'bin/openroad', '-no_init', '-exit',
                    HERE / 'receiver_electrical_repair.tcl'], env=environment,
                    stdout=log, stderr=-2, timeout=1800)
            report['stages'].append(dict(name='global', returncode=result.returncode,
                                         seconds=time.monotonic() - started))
            if result.returncode:
                raise ValueError('electrical repair/global routing failed; see global.log')
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                if abs(slack(work / ('before_' + kind + '.rpt'), mode) - proof[kind + '_ns']) > .001:
                    raise ValueError('electrical repair input timing differs by more than 1 ps')
            report['seed_connectivity'] = sizing_compare(candidate / 'after.v', work / 'before.v', routed / 'cells.v')
            report['filler_connectivity'] = buffer_compare(work / 'before.v', work / 'unfilled.v', defer_fillers=True)
            report['buffer_connectivity'] = repair_compare(work / 'unfilled.v', work / 'repaired.v', routed / 'cells.v')
            gc.collect()
            report['placement_connectivity'] = buffer_compare(work / 'repaired.v', work / 'global.v')
            report['global_estimates'] = dict(setup_ns=slack(work / 'global_setup.rpt', 'max'),
                                            hold_ns=slack(work / 'global_hold.rpt', 'min'))
            report['global_electrical_pass'] = 'VIOLATED' not in (work / 'global_electrical.rpt').read_text().upper()
            verify()
            report.update(status='global_repair_candidate', stage='complete',
                artifact_sha256={str(p): digest(p) for p in work.iterdir()
                                 if p.is_file() and p.name != 'checks.json'})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('seed', 'routed', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
