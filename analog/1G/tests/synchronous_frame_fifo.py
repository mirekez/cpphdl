#!/usr/bin/env python3
"""Regress stopped-clock prefill, bank/pointer wrap, PAUSE holds and real FIFO faults."""
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
    entry, fixture = ANALOG / '1G/synchronous_frame_fifo.cpp', SOURCE.with_suffix('.cpp')
    hashes = source_dependencies([entry, fixture], ANALOG.parent, args.cxx)
    for p in [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_transmit_crossing.py'),
              ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.native.resolve(), args.converter.resolve(),
              args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin'), Path(args.cxx).resolve()]:
        hashes[str(p)] = digest(p)
    report = dict(status='running', work=str(work), source_sha256=hashes)

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text); (args.work / 'checks.json').write_text(text)

    def command(name, argv):
        with (work / (name + '.log')).open('w') as log:
            execute(['prlimit', '--as=1073741824', '--', *map(str, argv)], cwd=work, env=os.environ,
                    stdout=log, stderr=-2, check=True, timeout=600, memory_exclusive=False)

    save()
    try:
        command('native', [args.native.resolve()])
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl', '--primary_clock', 'rx_clk',
                '136000000', '--secondary_clock', 'tx_clk', '125000000', entry, '--', '-I' + str(ANALOG.parent / 'include')])
        rtl = [work / ('rtl/' + n + '.sv') for n in ['Predef_pkg', 'SynchronousFrameFIFO']]
        command('build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '1', '--top-module', 'SynchronousFrameFIFO', '-Wno-fatal', '--Mdir', work / 'obj',
                '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [work / 'obj/VSynchronousFrameFIFO'])
        results = [(work / (flow + '.log')).read_text().strip() for flow in ['native', 'rtl']]
        expected = 'PASS 606 prefilled frames 12728 bytes 3 fault cases 65584 cycles '
        if any(not s.startswith(expected) or len(s.split()) != 13 for s in results) or results[0] != results[1]:
            raise ValueError('independent FIFO contract failed or native/RTL trajectory differs')
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification inputs changed')
        report.update(status='pass', frames=606, bytes=12728, fault_cases=3, cycles=65584, result=results[0],
                      rtl_sha256={str(p): digest(p) for p in rtl})
        print(results[0])
    except BaseException as error:
        report.update(status='fail', error=str(error)); raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['native', 'converter', 'verilator', 'work']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', required=True)
    run(parser.parse_args())
