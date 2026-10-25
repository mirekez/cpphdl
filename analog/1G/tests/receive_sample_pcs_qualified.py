#!/usr/bin/env python3
"""Qualify independently encoded Ethernet frames through fixed samples and PCS."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from hardware import TestToolchain, configure_cleanup, execute, source_dependencies
from receive_sample_recovery import archive, digest
from receive_sample_pcs_reference import generate
from receive_sample_pcs_qualified_trace import audit

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def metrics(path):
    rows = []
    pattern = re.compile(r'^case (\d+) master=(\d+) ppm=(\S+) delay_UI=(\S+) idle_errors=(\d+) frames=(\d+) bytes=(\d+) PASS$')
    for line in path.read_text().splitlines():
        match = pattern.fullmatch(line)
        if match:
            case, master, ppm, delay, idle_errors, frames, size = match.groups()
            rows.append((int(case), int(master), float(ppm), float(delay), int(frames), int(size)))
    expected = [(i, m, p, d, 2, 1598) for i, (m, p, d) in enumerate(
        (m, p, d) for m in (0, 1) for p in (-1500, 0, 1500) for d in (0, .105, .9))]
    if rows != expected:
        raise ValueError('missing frame/role/rate/delay case or wrong byte count')
    return rows


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    fixture = SOURCE.with_suffix('.cpp')
    entry = ANALOG / '1G/receive_sample_pcs_qualified.cpp'
    compiler = Path(shutil.which(args.cxx) or args.cxx).resolve()
    tools = [args.native.resolve(), args.converter.resolve(), args.verilator.resolve(), compiler]
    verilator_bin = args.verilator.resolve().with_name('verilator_bin')
    if verilator_bin.is_file():
        tools.append(verilator_bin)
    hashes = source_dependencies([fixture, entry], ANALOG.parent, args.cxx)
    helpers = [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_sample_recovery.py'),
               SOURCE.with_name('receive_sample_pcs_reference.py'), SOURCE.with_name('receive_sample_pcs_qualified_trace.py'), SOURCE.with_name('receive_timing_confidence_model.py'),
               SOURCE.with_name('transmit_reference.py'),
               SOURCE.with_name('pcs_data.csv'), SOURCE.with_name('pcs_controls.csv'),
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    hashes.update({str(p): digest(p) for p in tools + helpers})
    report = dict(status='running', work=str(work), source_sha256=hashes, stages={},
                  scope='hardware-derived readiness from accepted idles with immediate PMA; fixed-rate ADC samples to recovered symbols and independently IEEE-coded Ethernet frames; no analog or physical timing qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (root / 'checks.json').write_text(payload)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(toolchain.command(argv), cwd=work, env=toolchain.env, stdout=log, stderr=-2,
                    check=True, timeout=3600)

    save()
    try:
        generate(work / 'fixtures')
        command('native', [args.native.resolve(), work / 'fixtures', work / 'native.csv'])
        native_metrics = metrics(work / 'native.log')
        native_clocks = audit(work / 'native.csv', work / 'fixtures')
        report['stages']['native'] = dict(status='pass', cases=native_metrics, clock_audit=native_clocks)
        save()
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                           '--primary_clock', 'clk', '250000000', entry, '--',
                           '-I' + str(ANALOG.parent / 'include')])
        conversion = (work / 'convert.log').read_text()
        if 'MISSED CALL' in conversion or 'Missing register strobe' in conversion:
            raise ValueError('converter reported incomplete state scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv'] + sorted(
            p for p in (work / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')
        objects = work / 'rtl_obj'
        command('rtl_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                             '-j', '1', '--top-module', 'ReceiveSamplePCSQualified', '-Wno-fatal', '--Mdir', objects,
                             '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                             '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [objects / 'VReceiveSamplePCSQualified', work / 'fixtures', work / 'rtl.csv'])
        if (audit(work / 'rtl.csv', work / 'fixtures') != native_clocks or
                metrics(work / 'rtl.log') != native_metrics or digest(work / 'native.csv') != digest(work / 'rtl.csv')):
            raise ValueError('native/RTL full-clock or independently decoded frame mismatch')
        report['stages']['rtl'] = dict(status='pass', exact_native=True, cases=native_metrics)
        report['traces'] = {flow: archive(work / (flow + '.csv')) for flow in ('native', 'rtl')}
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification input changed during run')
        report.update(status='pass', rtl_sha256={str(p): digest(p) for p in rtl},
                      fixture_sha256={str(p): digest(p) for p in (work / 'fixtures').iterdir()})
        print('PASS 18 immediate-start hardware-qualified cases, 36 frames, 28764 bytes')
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
