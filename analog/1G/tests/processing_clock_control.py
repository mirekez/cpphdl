#!/usr/bin/env python3
"""Check oscillator calibration on independently scheduled reference/PCS edges."""
import argparse
import json
import os
from pathlib import Path
import tempfile

from hardware import configure_cleanup, execute, source_dependencies
from receive_dsp_crossing import digest
from receive_sample_recovery import archive

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    fixture = SOURCE.with_suffix('.cpp')
    entry = ANALOG / '1G/processing_clock_control.cpp'
    hashes = source_dependencies([fixture, entry], ANALOG.parent, args.cxx)
    for path in (SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_dsp_crossing.py'),
                 SOURCE.with_name('receive_sample_recovery.py'), ANALOG / 'VerilogA/trace_storage.py',
                 ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.native.resolve(),
                 args.converter.resolve(), args.verilator.resolve(),
                 args.verilator.resolve().with_name('verilator_bin'), Path(args.cxx).resolve()):
        hashes[str(path)] = digest(path)
    report = dict(status='running', source_sha256=hashes, work=str(work),
                  scope='digital trim acquisition and frequency readiness; electrical oscillator checked separately')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, env=os.environ, stdout=log, stderr=-2,
                    check=True, timeout=600, memory_exclusive=False)

    save()
    try:
        command('native', [args.native.resolve(), work / 'native.csv'])
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                            '--primary_clock', 'pcs_clk', '800000000',
                            '--secondary_clock', 'ref_clk', '125000000', entry,
                            '--', '-I' + str(ANALOG.parent / 'include')])
        if any(error in (work / 'convert.log').read_text()
               for error in ('MISSED CALL', 'Missing register strobe', "can't find wire")):
            raise ValueError('incomplete converted state scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv', work / 'rtl/ProcessingClockControl.sv']
        source = rtl[-1].read_text()
        for clock in ('ref_clk', 'pcs_clk'):
            if 'always_ff @(posedge ' + clock + ' or posedge reset)' not in source:
                raise ValueError('missing clock domain: ' + clock)
        if source.count('(* ASYNC_REG = "TRUE" *)') != 8:
            raise ValueError('missing counter/readiness synchronizer attributes')
        objects = work / 'rtl_obj'
        command('rtl_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                             '-j', '1', '--top-module', 'ProcessingClockControl', '-Wno-fatal',
                             '--Mdir', objects, '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                             '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command('rtl', [objects / 'VProcessingClockControl', work / 'rtl.csv'])
        expected = 'PASS processing clock: 5 tuned curves, 3 rejected curves, 6 load-step cases, 2 drift recoveries; 698640 clock events\n'
        for flow in ('native', 'rtl'):
            if (work / (flow + '.log')).read_text() != expected:
                raise ValueError('independent clock qualification checks did not complete')
            with (work / (flow + '.csv')).open() as stream:
                count = 0
                profiles = set()
                for line in stream:
                    fields = line.strip().split(',')
                    if len(fields) != 12 or int(fields[0]) != count:
                        raise ValueError('incomplete clock trajectory')
                    profiles.add(int(fields[1]))
                    count += 1
                if count != 698640:
                    raise ValueError('missing clock events')
                if profiles != set(range(16)):
                    raise ValueError('missing acquisition, load-step or frequency-fault scenario')
        if digest(work / 'native.csv') != digest(work / 'rtl.csv'):
            raise ValueError('native/RTL clock trajectories differ')
        report['traces'] = {flow: archive(work / (flow + '.csv')) for flow in ('native', 'rtl')}
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification inputs changed')
        report.update(status='pass', qualified_curves=5, rejected_curves=3, load_step_cases=6, drift_recoveries=2, events=698640,
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
