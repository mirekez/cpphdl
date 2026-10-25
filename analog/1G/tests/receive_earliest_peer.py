#!/usr/bin/env python3
"""Earliest legal peer frames with fixed ADC samples and RTL receive paths."""
import argparse
import json
import os
from pathlib import Path
import tempfile
import sys
from hardware import TestToolchain, configure_cleanup, execute, source_dependencies
from receive_sample_recovery import archive, digest

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / "VerilogA"))
from trace_storage import capture_trace


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    fixture, entry = SOURCE.with_suffix('.cpp'), ANALOG / '1G/receive_sample_pcs_qualified.cpp'
    hashes = source_dependencies([fixture, entry], ANALOG.parent, args.cxx)
    paths = [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_sample_recovery.py'),
             ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', ANALOG / 'VerilogA/trace_storage.py', args.native.resolve(),
             args.converter.resolve(), args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin'),
             Path(args.cxx).resolve()]
    if args.rtl_binary:
        paths.append(args.rtl_binary.resolve())
    hashes.update({str(p): digest(p) for p in paths})
    report = dict(status='running', source_sha256=hashes, work=str(work), cases=[],
                  scope='Native and RTL receive paths; causal native transmitter/PMA/channel fixture, no physical clock or metastability proof')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(toolchain.command(argv), cwd=work, env=toolchain.env, stdout=log, stderr=-2, check=True, timeout=1800)

    save()
    try:
        rtl = []
        if args.rtl_binary:
            rtl_binary = args.rtl_binary.resolve()
        else:
            command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl', '--primary_clock',
                                'clk', '250000000', entry, '--', '-I' + str(ANALOG.parent / 'include')])
            if 'MISSED CALL' in (work / 'convert.log').read_text():
                raise ValueError('incomplete converted state scheduling')
            rtl = [work / 'rtl/Predef_pkg.sv'] + sorted(p for p in (work / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')
            objects = work / 'rtl_obj'
            command('rtl_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                                 '-j', '1', '--top-module', 'ReceiveSamplePCSQualified', '-Wno-fatal', '--Mdir', objects,
                                 '-CFLAGS', '-DVERILATOR -std=c++17 -I' + str(ANALOG.parent / 'include'), *rtl, fixture,
                                 '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
            rtl_binary = objects / 'VReceiveSamplePCSQualified'
        cases = [(-1500, .105, 2)] if args.smoke else [(p, d, n) for n in (0, 2)
                 for p in (-1900, -1500, 0, 1500, 1900) for d in (0, .105, .9)]
        for index, (ppm, delay, noise) in enumerate(cases):
            traces = {}
            for flow, binary in [('native', args.native.resolve()), ('rtl', rtl_binary)]:
                label = str(index) + '_' + flow
                wave = work / (label + '.csv')
                with capture_trace(wave) as capture, capture_trace(Path(str(wave) + '.tx.csv')) as tx_capture:
                    command(label, [binary, ppm, delay, wave, noise, 1514])
                if 'PASS earliest peer frames' not in (work / (label + '.log')).read_text():
                    raise ValueError('missing earliest frame acceptance')
                traces[flow] = dict(wave=capture.metadata, tx=tx_capture.metadata)
            if any(traces['native'][kind]['original_sha256'] != traces['rtl'][kind]['original_sha256'] for kind in ('wave', 'tx')):
                raise ValueError('complete-clock receive/handshake or transmitter trace mismatch')
            artifacts = {}
            for kind in ('wave', 'tx'):
                artifacts[kind] = dict(shared_archive=traces['native'][kind], producer_raw_sha256={flow: traces[flow][kind]['original_sha256'] for flow in ('native', 'rtl')})
                # Both producer byte streams have been independently hashed and
                # compared. Retain one verified lossless archive for equal data.
                Path(traces['rtl'][kind]['storage_path']).unlink()
            report['cases'].append(dict(ppm=ppm, delay_UI=delay, noise_counts=noise,
                                        frames=2, bytes=3052, exact_native_rtl=True, artifacts=artifacts))
            save()
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('input changed during qualification')
        report.update(status='pass', rtl_sha256={str(p): digest(p) for p in rtl})
        print('PASS', len(cases), 'earliest peer cases, exact complete-clock native/RTL traces')
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
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--rtl-binary', type=Path, help='Reuse an explicitly hashed RTL executable; all-clock native comparison remains mandatory')
    run(parser.parse_args())
