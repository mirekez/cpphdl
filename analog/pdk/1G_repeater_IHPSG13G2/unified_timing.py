#!/usr/bin/env python3
"""Estimate or repair electrical loading from a saved common placement."""
import argparse
import json
import os
from pathlib import Path
import shutil
import time

from tool_process import configure_cleanup, execute
from unified_synthesis import digest, MACRO


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'placed_unrouted':
        raise ValueError('legal common placement required')
    hashes = {**proof['source_sha256'], **proof['artifact_sha256']}
    if any(digest(p) != value for p, value in hashes.items()):
        raise ValueError('placement checkpoint is stale')
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    if args.repair_electrical:
        for name in ('constraints.sdc', 'route_tech.lef'):
            shutil.copy2(args.proof.resolve().parent / name, work / name)
    source = Path(__file__).resolve().parent
    executable = args.tools.resolve() / 'bin/openroad'
    for path in (Path(__file__), source / 'unified_timing.tcl', source / 'tool_process.py',
                 source / 'unified_synthesis.py', executable, args.proof.resolve()):
        hashes[str(path)] = digest(path)
    refs = args.pdk.resolve() / 'ihp-sg13g2/libs.ref'
    for path in (refs / 'sg13g2_stdcell/lib').glob('*.lib'):
        hashes[str(path)] = digest(path)
    for path in (refs / 'sg13g2_sram/lib').glob(MACRO + '*.lib'):
        hashes[str(path)] = digest(path)
    report = dict(status='running', full_chip_qualified=False, source_sha256=hashes,
                  scope='Placement RC estimates only; ideal clocks, no routing/PEX or signoff.',
                  repair_electrical=args.repair_electrical,
                  memory_limit_mib=args.memory_mib)
    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    try:
        start = time.monotonic()
        environment = dict(os.environ, UNIFIED_WORK=str(work), UNIFIED_PDK=str(args.pdk.resolve()),
                           UNIFIED_PLACEMENT=str(args.proof.resolve().parent), OMP_NUM_THREADS='1',
                           UNIFIED_REPAIR=str(int(args.repair_electrical)))
        with (work / 'timing.log').open('w') as log:
            execute(['prlimit', '--as=' + str(args.memory_mib * 1024 * 1024), '--', str(executable),
                     '-no_init', '-exit', str(source / 'unified_timing.tcl')], cwd=work,
                    env=environment, stdout=log, stderr=-2, timeout=3600,
                    memory_exclusive=True, check=True)
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('timing inputs changed')
        artifacts = ['timing.rpt', 'setup.rpt', 'hold.rpt', 'constraints.rpt']
        if args.repair_electrical:
            artifacts.extend(('placed.odb', 'placed.def', 'placed.v', 'constraints.sdc',
                              'route_tech.lef', 'inventory.json', 'electrical.rpt'))
            report['inventory'] = json.loads((work / 'inventory.json').read_text())
        report.update(status='placed_unrouted' if args.repair_electrical else 'timing_estimated',
                      seconds=round(time.monotonic() - start, 3),
                      artifact_sha256={str(work / n): digest(work / n) for n in artifacts})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--memory-mib', type=int, default=3072)
    parser.add_argument('--repair-electrical', action='store_true',
                        help='buffer high fanout/load nets, resize drivers and relegalize cells')
    run(parser.parse_args())
