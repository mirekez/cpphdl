#!/usr/bin/env python3
"""Map a qualified two-port PAUSE datapath to IHP SRAM and standard cells."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / '1G/tests'))
from hardware import configure_cleanup, execute
from receive_transmit_crossing import digest

MACRO = 'RM_IHPSG13_2P_1024x16_c2_bm_bist'


def quoted(path):
    return json.dumps(str(path))


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'pass' or proof.get('sram', {}).get('status') != 'pass':
        raise ValueError('a current passing native/RTL/PDK SRAM proof is required')
    hashes = dict(proof['source_sha256'])
    hashes.update(proof['rtl_sha256'])
    inputs = Path(proof['work'])
    hashes[str(inputs / 'sram-mapped.v')] = proof['sram']['netlist_sha256']
    pdk = args.pdk.resolve() / 'ihp-sg13g2/libs.ref'
    liberty = pdk / 'sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    sram_liberty = pdk / ('sg13g2_sram/lib/' + MACRO + '_typ_1p20V_25C.lib')
    models = [pdk / ('sg13g2_sram/verilog/' + MACRO + '.v'),
              pdk / 'sg13g2_sram/verilog/RM_IHPSG13_2P_core_behavioral_bm_bist_ideal.v']
    binary = args.tools.resolve() / 'bin'
    for p in [SOURCE, args.proof.resolve(), liberty, sram_liberty, *models,
              *[binary / n for n in ['sv2v', 'yosys', 'abc']], args.verilator.resolve()]:
        hashes[str(p)] = digest(p)
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('functional proof is stale')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes,
                  scope='Functional IHP SRAM and standard-cell synthesis; physical timing/DRC/LVS remain separate.')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(name, argv, output=None):
        # The large frame arrays already map to SRAM. Bound the remaining
        # small control-logic synthesis and single-worker replay to 1.5 GiB.
        with (output or work / (name + '.log')).open('w') as log:
            execute(['prlimit', '--as=1610612736', '--', *map(str, argv)], cwd=work,
                    env=os.environ, stdout=log, stderr=-2, timeout=1200, check=True, memory_exclusive=False)

    save()
    try:
        rtl = [inputs / 'rtl/Predef_pkg.sv', *sorted(p for p in (inputs / 'rtl').glob('*.sv')
                if p.name not in ['Predef_pkg.sv', 'SynchronousFrameFIFO.sv']), inputs / 'sram-mapped.v']
        command('sv2v', [binary / 'sv2v', '+RTS', '-N1', '-RTS', *rtl], work / 'design.v')
        script = work / 'synth.ys'
        script.write_text(f'''read_liberty -lib {quoted(liberty)}
read_liberty -lib {quoted(sram_liberty)}
read_verilog {quoted(work / 'design.v')}
hierarchy -check -top FlowControlledRepeater
synth -top FlowControlledRepeater -flatten -noabc
dfflibmap -liberty {quoted(liberty)}
abc -exe {quoted(binary / 'abc')} -liberty {quoted(liberty)}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
stat -liberty {quoted(liberty)}
write_verilog -noattr -noexpr {quoted(work / 'mapped.v')}
write_json {quoted(work / 'mapped.json')}
''')
        command('synth', [binary / 'yosys', '-s', script])
        netlist = json.loads((work / 'mapped.json').read_text())['modules']['FlowControlledRepeater']
        counts = Counter(cell['type'] for cell in netlist['cells'].values())
        if counts[MACRO] != 8 or any(n != MACRO and not n.startswith('sg13g2_') for n in counts):
            raise ValueError('synthesis changed SRAM inventory or left unmapped logic')
        report['cells'] = dict(sorted(counts.items()))
        report['sram_instances'] = sorted(n for n, c in netlist['cells'].items() if c['type'] == MACRO)
        script = work / 'cells.ys'
        script.write_text(f'read_liberty -ignore_miss_func -ignore_miss_dir {quoted(liberty)}\n'
                          f'write_verilog -noattr {quoted(work / "cells.v")}\n')
        command('cells', [binary / 'yosys', '-s', script])
        objects = work / 'mapped_obj'
        command('build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '1', '--top-module', 'FlowControlledRepeater', '-Wno-fatal', '-DFUNCTIONAL', '-DSYNTHESIS',
                '--Mdir', objects, '-CFLAGS', '-DVERILATOR -std=c++17', work / 'mapped.v', work / 'cells.v',
                *models, ANALOG / '1G/tests/flow_controlled_repeater.cpp',
                '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('replay', [objects / 'VFlowControlledRepeater'])
        result = (work / 'replay.log').read_text().strip()
        if result != proof['result']:
            raise ValueError('mapped cells failed the independent contract or differ from native/RTL/SRAM')
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('synthesis inputs changed')
        report.update(status='pass', result=result,
                      artifact_sha256={str(work / n): digest(work / n) for n in ['mapped.v', 'mapped.json', 'cells.v']})
        print(result)
        print('Mapped', sum(counts.values()), 'instances including eight physical SRAM macros')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['proof', 'pdk', 'tools', 'verilator', 'work']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', required=True)
    run(parser.parse_args())
