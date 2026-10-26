#!/usr/bin/env python3
"""Place the verified IHP PAUSE datapath and eight SRAM banks for chip integration."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
COMMON = ANALOG / 'pdk/1G_repeater_IHPSG13G2'
sys.path[:0] = [str(SOURCE.parent), str(COMMON), str(ANALOG / '1G/tests')]
from tool_process import configure_cleanup, execute
from receive_transmit_crossing import digest
from route_tech import prepare
from flow_control_constraints import constraints


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'pass' or len(proof.get('sram_instances', [])) != 8:
        raise ValueError('current passing cell/SRAM synthesis and replay required')
    hashes = dict(proof['source_sha256'])
    hashes.update(proof['artifact_sha256'])
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    sources = [SOURCE, SOURCE.with_name('flow_control_place.tcl'), SOURCE.with_name('flow_control_constraints.py'),
               COMMON / 'route_tech.py', COMMON / 'tool_process.py', args.proof.resolve(), tools / 'bin/openroad']
    for family in ['sg13g2_stdcell', 'sg13g2_sram']:
        root = pdk / ('ihp-sg13g2/libs.ref/' + family)
        if family.endswith('stdcell'):
            sources += [root / ('lef/' + n) for n in ['sg13g2_tech.lef', 'sg13g2_stdcell.lef']]
            sources += [root / ('lib/' + n) for n in ['sg13g2_stdcell_slow_1p08V_125C.lib', 'sg13g2_stdcell_fast_1p32V_m40C.lib']]
        else:
            sources += list((root / 'lef').glob('RM_IHPSG13_2P_1024x16_c2_bm_bist.lef'))
            sources += list((root / 'lib').glob('RM_IHPSG13_2P_1024x16_c2_bm_bist_*.lib'))
    hashes.update({str(p): digest(p) for p in sources})
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('cell proof is stale')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes,
                  scope='Placement checkpoint for SRAM and control logic; excludes final route, extracted CDC, full DRC/LVS and chip assembly.')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        shutil.copyfile(Path(proof['work']) / 'mapped.v', work / 'mapped.v')
        (work / 'constraints.sdc').write_text(constraints())
        prepare(pdk, work)
        environment = dict(os.environ, FLOW_WORK=str(work), FLOW_PDK=str(pdk), OMP_NUM_THREADS='1')
        report['stage'] = 'waiting_for_physical_memory_slot'
        save()
        with (work / 'placement.log').open('w') as log:
            execute([tools / 'bin/openroad', '-no_init', '-exit', SOURCE.with_name('flow_control_place.tcl')],
                    cwd=work, env=environment, stdout=log, stderr=-2, timeout=14400,
                    check=True, memory_exclusive=True)
        artifacts = ['placed.odb', 'placed.def', 'placed.v', 'placed_timing.rpt', 'placed_setup.rpt',
                     'placed_hold.rpt', 'placed_electrical.rpt', 'route_tech.lef', 'constraints.sdc']
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('placement inputs changed')
        report.update(status='placement_checkpoint_only', stage='complete',
                      artifact_sha256={str(work / n): digest(work / n) for n in artifacts},
                      setup=(work / 'placed_setup.rpt').read_text().strip(),
                      hold=(work / 'placed_hold.rpt').read_text().strip())
        print(report['setup']); print(report['hold'])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['proof', 'pdk', 'tools', 'work']:
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
