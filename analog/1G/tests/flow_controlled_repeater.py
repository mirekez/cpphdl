#!/usr/bin/env python3
"""Check bilateral wire-level PAUSE and lossless forwarding in native C++ and RTL."""
import argparse
import json
import os
from pathlib import Path
import tempfile
from hardware import configure_cleanup, execute, source_dependencies
from receive_transmit_crossing import digest

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    entry = ANALOG / '1G/flow_controlled_repeater.cpp'
    fixture = SOURCE.with_suffix('.cpp')
    hashes = source_dependencies([entry, fixture], ANALOG.parent, args.cxx)
    tracked = [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_transmit_crossing.py'),
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.native.resolve(),
               args.converter.resolve(), args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin'),
               Path(args.cxx).resolve()]
    if args.sram_pdk:
        if not args.yosys or not args.sv2v:
            raise ValueError('SRAM mapping requires yosys and sv2v')
        sram = args.sram_pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_sram'
        macro = 'RM_IHPSG13_2P_1024x16_c2_bm_bist'
        models = [sram / ('verilog/' + macro + '.v'),
                  sram / 'verilog/RM_IHPSG13_2P_core_behavioral_bm_bist_ideal.v']
        tracked += [*models, args.yosys.resolve(), args.sv2v.resolve(),
                    ANALOG / '1G/IHPSG13G2/frame_sram.rules', ANALOG / '1G/IHPSG13G2/frame_sram_map.v']
        for suffix, subdir in [('gds', 'gds'), ('lef', 'lef'), ('cdl', 'cdl')]:
            tracked.append(sram / (subdir + '/' + macro + '.' + suffix))
        tracked += sorted((sram / 'lib').glob(macro + '_*.lib'))
    hashes.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', source_sha256=hashes, work=str(work),
                  scope='Functional MAC PAUSE integration with independent clocks and delayed peers. '
                        'Not physical CDC/timing, auto-negotiation integration or complete PHY/ASIC qualification.')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv, output=None):
        with (output or work / (label + '.log')).open('w') as log:
            execute(['prlimit', '--as=1610612736', '--', *map(str, argv)], cwd=work, env=os.environ,
                    stdout=log, stderr=-2, check=True, timeout=600, memory_exclusive=False)

    save()
    try:
        command('native', [args.native.resolve()])
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                '--primary_clock', 'rx_clk', '136000000', '--secondary_clock', 'tx_clk', '125000000',
                '--secondary_clock', 'b_rx_clk', '136000000', '--secondary_clock', 'a_tx_clk', '125000000',
                entry, '--', '-I' + str(ANALOG.parent / 'include')])
        if 'MISSED CALL' in (work / 'convert.log').read_text():
            raise ValueError('incomplete clock scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv', *sorted(p for p in (work / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')]
        for name, expected in [('EthernetPauseCrossing', 4), ('FrameCrossingReset', 8)]:
            if (work / ('rtl/' + name + '.sv')).read_text().count('(* ASYNC_REG = "TRUE" *)') != expected:
                raise ValueError('lost CDC implementation attributes: ' + name)
        objects = work / 'rtl_obj'
        command('rtl_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '1', '--top-module', 'FlowControlledRepeater', '-Wno-fatal', '--Mdir', objects,
                '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [objects / 'VFlowControlledRepeater'])
        results = [(work / (flow + '.log')).read_text().strip() for flow in ('native', 'rtl')]
        expected = 'PASS 3 duplex cases 240 intact frames 6 pauses 6 resumes 2 isolation cases 630084 cycles '
        if any(not result.startswith(expected) or len(result.split()) != 18 for result in results):
            raise ValueError('independent lossless forwarding contract did not finish')
        if results[0] != results[1]:
            raise ValueError('native/RTL full-cycle output digest differs')
        if args.sram_pdk:
            command('sv2v', [args.sv2v.resolve(), '+RTS', '-N1', '-RTS', work / 'rtl/Predef_pkg.sv',
                            work / 'rtl/SynchronousFrameFIFO.sv'], work / 'sram-input.v')
            script = work / 'sram.ys'
            script.write_text('read_verilog "' + str(work / 'sram-input.v') + '"\n' +
                'hierarchy -top SynchronousFrameFIFO\nproc\nopt\nmemory_dff\nmemory_collect\n' +
                'memory_bram -rules "' + str(ANALOG / '1G/IHPSG13G2/frame_sram.rules') + '"\n' +
                'techmap -map "' + str(ANALOG / '1G/IHPSG13G2/frame_sram_map.v') + '"\nopt_clean\n' +
                'write_verilog "' + str(work / 'sram-mapped.v') + '"\n')
            command('sram_map', [args.yosys.resolve(), '-s', script])
            mapped_text = (work / 'sram-mapped.v').read_text()
            if mapped_text.count(macro + ' ') != 4 or 'reg [10:0] words' in mapped_text:
                raise ValueError('frame storage was not mapped to exactly four physical SRAM banks')
            mapped_rtl = [p for p in rtl if p.name != 'SynchronousFrameFIFO.sv'] + [work / 'sram-mapped.v', *models]
            objects = work / 'sram_obj'
            command('sram_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                    '-j', '1', '--top-module', 'FlowControlledRepeater', '-Wno-fatal', '-DFUNCTIONAL', '-DSYNTHESIS',
                    '--Mdir', objects, '-CFLAGS', '-DVERILATOR -std=c++17', *mapped_rtl, fixture,
                    '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
            command('sram', [objects / 'VFlowControlledRepeater'])
            mapped_result = (work / 'sram.log').read_text().strip()
            if not mapped_result.startswith(expected) or mapped_result != results[0]:
                raise ValueError('PDK SRAM replay failed independent contract or differs from C++/RTL')
            report['sram'] = dict(status='pass', macro=macro, banks_per_direction=4, total_banks=8,
                                  netlist_sha256=digest(work / 'sram-mapped.v'), result=mapped_result,
                                  scope='PDK functional SRAM model; excludes extracted timing and physical DRC/LVS')
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification inputs changed')
        report.update(status='pass', duplex_cases=3, intact_data_frames=240, pause_frames=6, resume_frames=6,
                      isolation_cases=2, cycles=630084, result=results[0], rtl_sha256={str(p): digest(p) for p in rtl})
        print(results[0])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native', 'converter', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', required=True)
    for name in ('sram-pdk', 'yosys', 'sv2v'):
        parser.add_argument('--' + name, type=Path)
    run(parser.parse_args())
