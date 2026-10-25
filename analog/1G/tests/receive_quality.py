#!/usr/bin/env python3
"""Native/RTL qualification of accepted-idle readiness and immediate revocation."""
import argparse
import json
import os
from pathlib import Path
import tempfile
from hardware import TestToolchain, configure_cleanup, execute, source_dependencies
from receive_sample_recovery import archive, digest

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    fixture, entry = SOURCE.with_suffix('.cpp'), ANALOG / '1G/receive_quality.cpp'
    hashes = source_dependencies([fixture, entry], ANALOG.parent, args.cxx)
    paths = [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_sample_recovery.py'),
             ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.native.resolve(),
             args.converter.resolve(), args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin'),
             Path(args.cxx).resolve()]
    hashes.update({str(p): digest(p) for p in paths})
    report = dict(status='running', source_sha256=hashes, work=str(work))

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(toolchain.command(argv), cwd=work, env=toolchain.env, stdout=log, stderr=-2, check=True, timeout=1800)

    save()
    try:
        command('native', [args.native.resolve(), work / 'native.csv'])
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl', '--primary_clock',
                            'clk', '250000000', entry, '--', '-I' + str(ANALOG.parent / 'include')])
        if 'MISSED CALL' in (work / 'convert.log').read_text():
            raise ValueError('incomplete converted state scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv', work / 'rtl/ReceiveQuality.sv']
        objects = work / 'rtl_obj'
        command('rtl_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                             '-j', '1', '--top-module', 'ReceiveQuality', '-Wno-fatal', '--Mdir', objects,
                             '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                             '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [objects / 'VReceiveQuality', work / 'rtl.csv'])
        expected = 'PASS 1813 clocks, 8 acquisitions, 8 revocations; symbol-count qualification and reset/fault priority\n'
        if any((work / (flow + '.log')).read_text() != expected for flow in ('native', 'rtl')):
            raise ValueError('missing independent state/fault checks')
        if digest(work / 'native.csv') != digest(work / 'rtl.csv'):
            raise ValueError('full-clock native/RTL mismatch')
        for flow in ('native', 'rtl'):
            rows = (work / (flow + '.csv')).read_text().splitlines()
            if len(rows) != 1813 or any(len(r.split(',')) != 11 or int(r.split(',')[0]) != i for i, r in enumerate(rows)):
                raise ValueError('missing complete clock state')
        report['traces'] = {flow: archive(work / (flow + '.csv')) for flow in ('native', 'rtl')}
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('input changed during qualification')
        report.update(status='pass', clocks=1813, acquisitions=8, revocations=8,
                      rtl_sha256={str(p): digest(p) for p in rtl})
        print(expected.strip())
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
    run(parser.parse_args())
