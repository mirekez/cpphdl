#!/usr/bin/env python3
"""Check complete symbol bundles on independent ADC and PCS clock edges."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
from hardware import configure_cleanup, execute, source_dependencies

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from trace_storage import archive_trace


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    fixture = SOURCE.with_suffix('.cpp')
    entry = ANALOG / '1G/receive_symbol_crossing.cpp'
    hashes = source_dependencies([fixture, entry], ANALOG.parent, args.cxx)
    paths = [SOURCE, SOURCE.with_name('hardware.py'), ANALOG / 'VerilogA/trace_storage.py',
             ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.native.resolve(),
             args.converter.resolve(), args.verilator.resolve(),
             args.verilator.resolve().with_name('verilator_bin'), Path(args.cxx).resolve()]
    hashes.update({str(p): digest(p) for p in paths})
    report = dict(status='running', source_sha256=hashes, work=str(work),
                  scope='Functional clock-domain transfer; no metastability, routed skew or clock-generator signoff')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, env=os.environ, stdout=log, stderr=-2,
                    check=True, timeout=600)

    save()
    try:
        command('native', [args.native.resolve(), work / 'native.csv'])
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                            '--primary_clock', 'adc_clk', '250000000',
                            '--secondary_clock', 'pcs_clk', '142857143', entry,
                            '--', '-I' + str(ANALOG.parent / 'include')])
        if 'MISSED CALL' in (work / 'convert.log').read_text():
            raise ValueError('incomplete converted state scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv', work / 'rtl/ReceiveSymbolCrossing.sv']
        text = rtl[-1].read_text()
        if text.count('(* ASYNC_REG = "TRUE" *)') != 6:
            raise ValueError('missing Gray-pointer or fault synchronizer attributes')
        for clock in ('adc_clk', 'pcs_clk'):
            if 'always_ff @(posedge ' + clock + ')' not in text:
                raise ValueError('missing separate clock process: ' + clock)
        objects = work / 'rtl_obj'
        command('rtl_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                             '-j', '1', '--top-module', 'ReceiveSymbolCrossing', '-Wno-fatal',
                             '--Mdir', objects, '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                             '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [objects / 'VReceiveSymbolCrossing', work / 'rtl.csv'])
        expected = ('70 clock-phase/rate cases, 70047 exact symbol bundles, 236950 events; '
                    'tag wrap, clipping, pause, overflow, fault and reset passed\n')
        if any((work / (flow + '.log')).read_text() != expected for flow in ('native', 'rtl')):
            raise ValueError('independent bundle checks did not complete')
        if digest(work / 'native.csv') != digest(work / 'rtl.csv'):
            raise ValueError('native/RTL event trajectories differ')
        for flow in ('native', 'rtl'):
            with (work / (flow + '.csv')).open() as stream:
                count = 0
                for row in stream:
                    fields = row.strip().split(',')
                    if len(fields) != 10 or int(fields[0]) != count:
                        raise ValueError('missing complete event state')
                    count += 1
                if count != 236950:
                    raise ValueError('incomplete event trajectory')
        report['traces'] = {}
        for flow in ('native', 'rtl'):
            path, metadata = archive_trace(work / (flow + '.csv'))
            report['traces'][flow] = dict(path=str(path), **metadata)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification inputs changed')
        report.update(status='pass', cases=70, bundles=70047, events=236950,
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
