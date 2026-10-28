#!/usr/bin/env python3
"""Bounded native/RTL check of PAUSE capability resolution and page arbitration."""
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
    top = 'Autonegotiation' if args.link else 'Autonegotiator'
    entry = ANALOG / ('1G/autonegotiation.cpp' if args.link else '1G/negotiation.cpp')
    fixture = SOURCE.with_name('autoneg_link.cpp' if args.link else 'negotiation.cpp')
    hashes = source_dependencies([entry, fixture], ANALOG.parent, args.cxx)
    for p in [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_transmit_crossing.py'),
              ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.native.resolve(),
              args.converter.resolve(), args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin'), Path(args.cxx).resolve()]:
        hashes[str(p)] = digest(p)
    report = dict(status='running', work=str(work), source_sha256=hashes,
                  scope='Digital FLP wire exchange and PAUSE resolution' if args.link else 'Page controller PAUSE resolution; not FLP electrical or physical timing.')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(name, argv):
        with (work / (name + '.log')).open('w') as log:
            execute(['prlimit', '--as=1073741824', '--', *map(str, argv)], cwd=work, env=os.environ,
                    stdout=log, stderr=-2, check=True, timeout=1200, memory_exclusive=False)

    save()
    try:
        command('native', [args.native.resolve(), work / 'native.csv'])
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                '--primary_clock', 'clk', '125000000', entry, '--', '-I' + str(ANALOG.parent / 'include')])
        names = ['Predef_pkg', 'MasterSlaveResolver', 'Autonegotiator']
        if args.link: names += ['ReferenceMillisecond', 'FLPTransmitter', 'FLPReceiver', 'Autonegotiation']
        rtl = [work / ('rtl/' + n + '.sv') for n in names]
        command('build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '1', '--top-module', top, '-Wno-fatal', '--Mdir', work / 'obj',
                '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [work / ('obj/V' + top), work / 'rtl.csv'])
        for flow in ['native', 'rtl']:
            log = (work / (flow + '.log')).read_text()
            lines = (work / (flow + '.csv')).read_text().splitlines()
            if args.link:
                if '264750017 reference cycles; wire-monitored bidirectional FLP negotiation' not in log:
                    raise ValueError('full FLP wire contract incomplete')
                if not lines[-1].startswith('complete,264750017,'):
                    raise ValueError('missing FLP completion observation')
            else:
                if not log.startswith('28883 page-arbitration observations;'):
                    raise ValueError('page arbitration contract incomplete')
                if len(lines) != 28883 or any(len(s.split(',')) != 4 or int(s.split(',')[0]) != i for i, s in enumerate(lines)):
                    raise ValueError('incomplete output observations')
        if digest(work / 'native.csv') != digest(work / 'rtl.csv'):
            raise ValueError('native/RTL page trajectories differ')
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification inputs changed')
        report.update(status='pass', observations=264750017 if args.link else 28883, peer_pause_abilities=[1024] if args.link else [0, 1024, 2048, 3072],
                      rtl_sha256={str(p): digest(p) for p in rtl}, trace_sha256=digest(work / 'native.csv'))
        print('PASS full FLP wire exchange, symmetric PAUSE and 264750017 cycles' if args.link else
              'PASS 28883 exact page-arbitration observations and four peer PAUSE capabilities')
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
    parser.add_argument('--link', action='store_true')
    run(parser.parse_args())
