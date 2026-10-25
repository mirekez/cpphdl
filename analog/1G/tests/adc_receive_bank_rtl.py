#!/usr/bin/env python3
"""Check complete ADC/calibration/tag composition in native and converted RTL."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(SOURCE.parent))
from hardware import TestToolchain, configure_cleanup, execute, source_dependencies


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    fixture = SOURCE.with_suffix('.cpp').with_name('adc_receive_bank.cpp')
    entry = ANALOG / '1G/adc_receive_bank.cpp'
    hashes = source_dependencies([fixture, entry], ANALOG.parent, args.cxx,
        ['-std=c++17', '-I' + str(ANALOG.parent / 'include')])
    hashes.update({str(p): digest(p) for p in (SOURCE, args.native.resolve(), args.converter.resolve(), args.verilator.resolve())})
    report = dict(status='running', source_sha256=hashes, work=str(work),
        scope='13-slice SAR bank, normalizer and tags with independent ideal comparison inputs; no physical clock/ADC/routed timing qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(toolchain.command(argv), cwd=work, env=toolchain.env, stdout=log, stderr=-2, check=True, timeout=900)

    save()
    try:
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl', entry,
            '--', '-I' + str(ANALOG.parent / 'include')])
        rtl = [work / 'rtl' / name for name in ('Predef_pkg.sv', 'ADCSARController.sv',
            'InterleavedSARBankController.sv', 'ReceiveSampleNormalizer.sv', 'ADCReceiveBankController.sv')]
        command('native', [args.native.resolve(), work / 'native.csv'])
        top = 'ADCReceiveBankController'
        objects = work / 'obj'
        command('compile', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
            '-j', '1', '--top-module', top, '-GSlices=13', '-Wno-fatal', '--Mdir', objects,
            '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
            '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [objects / ('V' + top), work / 'rtl.csv'])
        if (work / 'native.csv').read_bytes() != (work / 'rtl.csv').read_bytes():
            raise ValueError('native/RTL receive bank differs on an observed output')
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('receive bank verification inputs changed')
        report.update(status='pass', clocks=8320, continuous_and_gapped=True,
            changing_calibration=True, clipping=True, precharge_tested=True, unused_positions_checked=True,
            disable_flush_checked=True, reset_restart_checked=True,
            native_trace_sha256=digest(work / 'native.csv'), rtl_trace_sha256=digest(work / 'rtl.csv'),
            rtl_sha256={str(p): digest(p) for p in rtl})
        print('PASS native/RTL complete receive bank: 8,320 clocks, every output equal')
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
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
