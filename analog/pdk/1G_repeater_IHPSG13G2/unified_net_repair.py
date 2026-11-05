#!/usr/bin/env python3
"""Repair selected loaded nets without discarding a common placement."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import time

from tool_process import configure_cleanup, execute
from unified_synthesis import digest, MACRO


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'placed_unrouted':
        raise ValueError('legal common placement required')
    hashes = {**proof['source_sha256'], **proof['artifact_sha256']}
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('stale placement')
    if not args.driver or len(args.driver) != len(set(args.driver)) or any(
            not re.fullmatch(r'[A-Za-z0-9_./\[\]]+', n) for n in args.driver):
        raise ValueError('unique physical driver instance names required')
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    for name in ('constraints.sdc', 'route_tech.lef'):
        shutil.copy2(args.proof.resolve().parent / name, work / name)
    (work / 'drivers.txt').write_text('\n'.join(args.driver) + '\n')
    source = Path(__file__).resolve().parent
    executable = args.tools.resolve() / 'bin/openroad'
    for path in (Path(__file__), source / 'unified_net_repair.tcl', source / 'tool_process.py',
                 source / 'unified_placement_check.tcl',
                 source / 'unified_synthesis.py', executable, args.proof.resolve(), work / 'drivers.txt'):
        hashes[str(path)] = digest(path)
    refs = args.pdk.resolve() / 'ihp-sg13g2/libs.ref'
    for directory, pattern in [('sg13g2_stdcell', '*.lib'), ('sg13g2_sram', MACRO + '*.lib')]:
        for path in (refs / directory / 'lib').glob(pattern):
            hashes[str(path)] = digest(path)
    report = dict(status='running', full_chip_qualified=False, source_sha256=hashes,
                  scope='Selected-net electrical repair; placement RC and ideal clocks only.',
                  drivers=args.driver, memory_limit_mib=args.memory_mib, timing_analysis='not_run')
    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    try:
        start = time.monotonic()
        environment = dict(os.environ, UNIFIED_WORK=str(work), UNIFIED_PDK=str(args.pdk.resolve()),
                           UNIFIED_PLACEMENT=str(args.proof.resolve().parent), OMP_NUM_THREADS='1',
                           UNIFIED_EXPORT_TEXT=str(int(args.export_text)))
        with (work / 'repair.log').open('w') as log:
            execute(['prlimit', '--as=' + str(args.memory_mib * 1024 * 1024), '--', str(executable),
                     '-no_init', '-exit', str(source / 'unified_net_repair.tcl')], cwd=work,
                    env=environment, stdout=log, stderr=-2, timeout=args.timeout,
                    memory_exclusive=True, check=True)
        with (work / 'placement-validation.log').open('w') as log:
            execute(['prlimit', '--as=' + str(args.memory_mib * 1024 * 1024), '--', str(executable),
                     '-no_init', '-exit', str(source / 'unified_placement_check.tcl')], cwd=work,
                    env=environment, stdout=log, stderr=-2, timeout=300,
                    memory_exclusive=True, check=True)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('repair inputs changed')
        artifacts = ['placed.odb', 'constraints.sdc', 'route_tech.lef', 'nets.tsv',
                     'inventory.json', 'placement-validation.log']
        if args.export_text:
            artifacts.extend(('placed.def', 'placed.v'))
        report.update(status='placed_unrouted', seconds=round(time.monotonic() - start, 3),
                      inventory=json.loads((work / 'inventory.json').read_text()),
                      artifact_sha256={str(work / n): digest(work / n) for n in artifacts})
        # The final database has been independently read and checked. The
        # earlier unlegalized checkpoint no longer needs a second full copy.
        (work / 'repair.odb').unlink()
    except BaseException as error:
        report.update(status='fail', error=str(error))
        if (work / 'repair.odb').exists():
            report['intermediate_checkpoint'] = dict(path=str(work / 'repair.odb'),
                sha256=digest(work / 'repair.odb'), legalized=False)
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--driver', action='append', required=True)
    parser.add_argument('--memory-mib', type=int, default=4608)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--export-text', action='store_true', help='also export DEF/Verilog in the fresh validation process')
    run(parser.parse_args())
