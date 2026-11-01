#!/usr/bin/env python3
"""Exercise physical-rate negotiation through the actual PAUSE forwarding top."""
import argparse
import json
import os
from pathlib import Path
import re
import tempfile
from hardware import configure_cleanup, execute, source_dependencies
from receive_transmit_crossing import digest

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    entry = ANALOG / '1G/negotiated_repeater.cpp'
    fixture = SOURCE.with_suffix('.cpp')
    hashes = source_dependencies([entry, fixture], ANALOG.parent, args.cxx)
    tracked = [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_transmit_crossing.py'),
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.native.resolve(),
               args.converter.resolve(), args.verilator.resolve(),
               args.verilator.resolve().with_name('verilator_bin'), Path(args.cxx).resolve()]
    hashes.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', work=str(work), source_sha256=hashes,
                  scope='Digital full-rate FLP negotiation connected to PAUSE forwarding, '
                        'including stopped-clock link loss. Excludes electrical FLP, physical CDC, '
                        'slave TX clock recovery and full-chip qualification.')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(name, argv):
        with (work / (name + '.log')).open('w') as log:
            execute(['prlimit', '--as=1610612736', '--', *map(str, argv)], cwd=work, env=os.environ,
                    stdout=log, stderr=-2, check=True, timeout=1800, memory_exclusive=False)

    save()
    try:
        command('native', [args.native.resolve()])
        # The converter requires its primary domain to have the highest timing
        # budget. A conservative 136 MHz budget preserves legacy _work methods
        # on ref_clk; the physical reference and the fixture remain 125 MHz.
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                '--primary_clock', 'ref_clk', '136000000',
                '--secondary_clock', 'rx_clk', '136000000',
                '--secondary_clock', 'tx_clk', '125000000',
                '--secondary_clock', 'b_rx_clk', '136000000',
                '--secondary_clock', 'a_tx_clk', '125000000',
                entry, '--', '-I' + str(ANALOG.parent / 'include')])
        if 'MISSED CALL' in (work / 'convert.log').read_text():
            raise ValueError('incomplete clock scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv', *sorted(p for p in (work / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')]
        objects = work / 'obj'
        command('build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '1', '--top-module', 'NegotiatedRepeater', '-Wno-fatal', '--Mdir', objects,
                '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [objects / 'VNegotiatedRepeater'])
        results = []
        expression = (r'PASS negotiated repeater (\d+) reference cycles 2 intact frames PAUSE and '
                      r'stopped-clock loss (\d+) observations (\d+) digest')
        for flow in ('native', 'rtl'):
            result = (work / (flow + '.log')).read_text().strip().splitlines()[-1]
            match = re.fullmatch(expression, result)
            if not match or not 264000000 <= int(match[1]) < 266000000 or int(match[2]) <= int(match[1]) + 10000:
                raise ValueError('incomplete independent integration contract: ' + flow)
            results.append(result)
        if results[0] != results[1]:
            raise ValueError('native/RTL cycle digest differs')
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification inputs changed')
        report.update(status='pass', result=results[0],
                      rtl_sha256={str(p): digest(p) for p in rtl})
        print(results[0])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['native', 'converter', 'verilator', 'work']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', required=True)
    run(parser.parse_args())
