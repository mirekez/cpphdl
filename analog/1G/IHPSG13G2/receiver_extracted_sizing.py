#!/usr/bin/env python3
"""Evaluate receiver drive sizing with routed RC; never promote it as physical closure."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
COMMON = ROOT / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(COMMON))
from recheck_timing import timing_contract
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def slack(path, kind):
    values = re.findall(r'worst slack ' + kind + r'\s+([-+\deE.]+)', path.read_text())
    if len(values) != 1 or not math.isfinite(float(values[0])):
        raise ValueError('missing finite slack: ' + str(path))
    return float(values[0])


def run(args):
    original, work = args.routed.resolve(), args.output.resolve()
    evidence = json.loads((original / 'physical_module.json').read_text())
    if evidence.get('top') != 'PCSReceiver':
        raise ValueError('receiver implementation required')
    constraints, sdc = timing_contract(evidence)
    work.mkdir(parents=True, exist_ok=False)
    (work / 'input.sdc').write_text(sdc)
    paths = [original / name for name in ('core.odb', 'core.spef', 'cells.v', 'physical_module.json')]
    paths += [work / 'input.sdc', Path(__file__).resolve(), HERE / 'receiver_extracted_sizing.tcl',
              HERE / 'sizing_equivalence.py', HERE / 'buffer_equivalence.py', HERE / 'recheck_timing.py',
              HERE / 'physical_module.py', COMMON / 'tool_process.py']
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib'
    paths += [library / ('sg13g2_stdcell_' + corner + '.lib')
              for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C')]
    paths += [args.tools.resolve() / name for name in ('bin/openroad', 'root/usr/bin/openroad')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(),
                  source_sha256=hashes, constraints=constraints, stages=[],
                  timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    def verify():
        for name, value in hashes.items():
            if digest(name) != value:
                raise ValueError('receiver sizing input changed: ' + name)

    save()
    try:
        with memory_slot(['receiver-extracted-sizing'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, RECEIVER_ROUTED_WORK=str(original), REPEATER_WORK=str(work),
                               REPEATER_PDK=str(args.pdk.resolve()), OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())

            def stage(name, command, timeout):
                report['stage'] = name
                save()
                with (work / (name + '.log')).open('w') as log:
                    result = execute(command, env=environment, stdout=log, stderr=-2,
                                     timeout=timeout, memory_exclusive=False)
                report['stages'].append(dict(name=name, returncode=result.returncode))
                save()
                if result.returncode:
                    raise ValueError(name + ' failed; see ' + str(work / (name + '.log')))
                verify()

            stage('sizing', ['prlimit', '--as=4294967296', '--', args.tools.resolve() / 'bin/openroad',
                            '-no_init', '-exit', HERE / 'receiver_extracted_sizing.tcl'], 3600)
            stage('equivalence', [sys.executable, HERE / 'sizing_equivalence.py', '--before', work / 'before.v',
                                 '--after', work / 'after.v', '--models', original / 'cells.v',
                                 '--output', work / 'equivalence.json'], 600)
            report['estimates'] = {name: dict(setup_ns=slack(work / (name + '_setup.rpt'), 'max'),
                                            hold_ns=slack(work / (name + '_hold.rpt'), 'min'))
                                   for name in ('before', 'sized')}
            report.update(status='diagnostic_complete', stage='complete',
                          resized_cells=json.loads((work / 'equivalence.json').read_text())['resized_cells'],
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
    parser.add_argument('--routed', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
