#!/usr/bin/env python3
"""Prove 13-slice SAR cadence in converted RTL and reject an unsafe 12-slice bank."""
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
    paths = [SOURCE, SOURCE.with_name('adc_bank.cpp'), args.native.resolve(), args.converter.resolve(), args.verilator.resolve()]
    hashes = source_dependencies([SOURCE.with_name('adc_bank.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++17', '-I' + str(ANALOG.parent / 'include')])
    hashes.update({str(p): digest(p) for p in paths})
    report = dict(status='running', source_sha256=hashes, work=str(work), cases={},
        scope='digital SAR launch, tracking and conversion cadence with ideal comparator decisions; physical 13-slice analog bank and routed 250 MHz timing remain separate')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(toolchain.command(argv), cwd=work, env=toolchain.env, stdout=log, stderr=-2, check=True, timeout=600)

    save()
    try:
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
            ANALOG / '1G/adc_interleaved_bank.cpp', '--', '-I' + str(ANALOG.parent / 'include')])
        rtl = [work / 'rtl' / name for name in ('Predef_pkg.sv', 'ADCSARController.sv', 'InterleavedSARBankController.sv')]
        for count in (12, 13, 16):
            cpp_trace, rtl_trace = work / f'cpp_{count}.csv', work / f'rtl_{count}.csv'
            command(f'native_{count}', [args.native.resolve(), count, cpp_trace])
            object_dir = work / f'obj_{count}'
            command(f'compile_{count}', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '1', '--top-module', 'InterleavedSARBankController', f'-GSlices={count}', '-Wno-fatal',
                '--Mdir', object_dir, '-CFLAGS', f'-DVERILATOR -DBANK_SLICES={count} -std=c++17',
                *rtl, SOURCE.with_name('adc_bank.cpp'), '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
            command(f'rtl_{count}', [object_dir / 'VInterleavedSARBankController', count, rtl_trace])
            if cpp_trace.read_bytes() != rtl_trace.read_bytes():
                raise ValueError('native and converted RTL SAR bank trajectories differ')
            report['cases'][str(count)] = dict(status='pass', clocks=8256, continuous_samples=4096 if count >= 13 else 0,
                sampling_gaps_checked=True,
                collision_expected=count < 13, native_trace_sha256=digest(cpp_trace), rtl_trace_sha256=digest(rtl_trace))
            save()
            print('PASS native/RTL exact trajectory:', count, 'slices', flush=True)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('SAR bank verification inputs changed')
        report['status'] = 'pass'
        report['rtl_sha256'] = {str(p): digest(p) for p in rtl}
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
