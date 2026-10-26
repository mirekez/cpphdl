#!/usr/bin/env python3
"""Independent PAUSE contracts in native C++ and converted RTL, then exact comparison."""
import argparse
import json
import os
from pathlib import Path
import tempfile
from hardware import configure_cleanup, execute, source_dependencies
from receive_transmit_crossing import digest

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
NAMES = ('Receive', 'Transmit', 'Timer', 'Filter')
EXPECTED = ((20, 2904), (4, 342), (6, 4200515), (4, 3886))


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    entry = ANALOG / '1G/ethernet_pause.cpp'
    fixture = SOURCE.with_suffix('.cpp')
    hashes = source_dependencies([entry, fixture], ANALOG.parent, args.cxx, ['-DPAUSE_KIND=0'])
    tracked = [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_transmit_crossing.py'),
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.converter.resolve(),
               args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin'),
               Path(args.cxx).resolve(), *[p.resolve() for p in args.native]]
    hashes.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', source_sha256=hashes, work=str(work), modules={},
                  scope='PAUSE frame recognition, generation and timer. Not forwarding integration or physical timing.')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv):
        # Small control-only modules: cap address space and use one build worker.
        with (work / (label + '.log')).open('w') as log:
            execute(['prlimit', '--as=1073741824', '--', *map(str, argv)], cwd=work,
                    env=os.environ, stdout=log, stderr=-2, check=True, timeout=600, memory_exclusive=False)

    save()
    try:
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                           '--primary_clock', 'clk', '125000000', entry, '--', '-I' + str(ANALOG.parent / 'include')])
        if 'MISSED CALL' in (work / 'convert.log').read_text():
            raise ValueError('incomplete state scheduling')
        for index, name in enumerate(NAMES):
            top = 'EthernetPause' + name
            objects = work / ('obj_' + name)
            rtl = [work / 'rtl/Predef_pkg.sv', work / ('rtl/' + top + '.sv')]
            if name == 'Filter': rtl.append(work / 'rtl/EthernetPauseReceive.sv')
            command(name + '_native', [args.native[index].resolve()])
            command(name + '_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                    '-j', '1', '--top-module', top, '-Wno-fatal', '--Mdir', objects,
                    '-CFLAGS', '-DVERILATOR -DPAUSE_KIND=' + str(index) + ' -std=c++17', *rtl, fixture,
                    '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
            command(name + '_rtl', [objects / ('V' + top)])
            results = [(work / (name + '_' + flow + '.log')).read_text().strip() for flow in ('native', 'rtl')]
            for result in results:
                tokens = result.split()
                if len(tokens) != 5 or tokens[0] != 'PASS' or tuple(map(int, tokens[1:4])) != (index, *EXPECTED[index]):
                    raise ValueError('independent contract did not finish: ' + name)
            if results[0] != results[1]:
                raise ValueError('native/RTL cycle digest mismatch: ' + name)
            report['modules'][name] = dict(status='pass', cases=EXPECTED[index][0], cycles=EXPECTED[index][1],
                                           full_cycle_digest=results[0].split()[-1],
                                           rtl_sha256={str(p): digest(p) for p in rtl})
            save()
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification inputs changed')
        report['status'] = 'pass'
        print('PASS PAUSE receive/transmit/timer/filter: 34 independent cases, native/RTL cycle digests match')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('converter', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--native', type=Path, nargs=4, required=True)
    parser.add_argument('--cxx', required=True)
    run(parser.parse_args())
