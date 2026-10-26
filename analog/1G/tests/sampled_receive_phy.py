#!/usr/bin/env python3
"""Check ADC-tag context alignment and the connected sample-to-Ethernet path."""
import argparse
import json
import math
from pathlib import Path
import shutil
import tempfile
import re

from hardware import TestToolchain, configure_cleanup, execute, source_dependencies
from receive_sample_recovery import archive, digest
from receive_dsp_pcs_reference import generate as generate_pcs
from autonomous_receive_pcs_reference import generate as generate_autonomous_pcs

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    stem, module = {'phy': ('sampled_receive_phy', 'SampledReceivePHY'),
                    'context': ('receive_sample_context', 'ReceiveSampleContext'),
                    'capture': ('adc_receive_capture', 'ADCReceiveCapture'),
                    'adc_phy': ('adc_receive_phy', 'ADCReceivePHY'),
                    'clock': ('reference_clock_monitor', 'ReferenceClockMonitor'),
                    'clocked_phy': ('clocked_receive_phy', 'ClockedReceivePHY'),
                    'async_phy': ('asynchronous_receive_phy', 'AsynchronousReceivePHY'),
                    'autonomous_pcs': ('autonomous_receive_pcs', 'AutonomousReceivePCS'),
                    'autonomous_phy': ('autonomous_receive_phy', 'AutonomousReceivePHY'),
                    'autonomous_adc_phy': ('autonomous_adc_receive_phy', 'AutonomousADCReceivePHY'),
                    'qualified_adc_phy': ('qualified_adc_receive_phy', 'QualifiedADCReceivePHY')}[args.case]
    is_phy = args.case in ('phy', 'adc_phy', 'clocked_phy', 'async_phy', 'autonomous_pcs', 'autonomous_phy', 'autonomous_adc_phy', 'qualified_adc_phy')
    fixture = SOURCE.with_name(stem + '.cpp')
    entry = ANALOG / ('1G/' + stem + '.cpp')
    compiler = Path(shutil.which(args.cxx) or args.cxx).resolve()
    tools = [args.native.resolve(), args.converter.resolve(), args.verilator.resolve(), compiler]
    verilator_bin = args.verilator.resolve().with_name('verilator_bin')
    if verilator_bin.is_file():
        tools.append(verilator_bin)
    helpers = [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_sample_recovery.py'),
               SOURCE.with_name('receive_dsp_pcs_reference.py'), SOURCE.with_name('transmit_reference.py'),
               ANALOG.parent / 'tests/TestToolchain.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    if is_phy:
        helpers += [SOURCE.with_name('pcs_data.csv'), SOURCE.with_name('pcs_controls.csv')]
    helpers.append(SOURCE.with_name('autonomous_receive_pcs_reference.py'))
    hashes = source_dependencies([fixture, entry], ANALOG.parent, str(compiler))
    hashes.update({str(p): digest(p) for p in tools + helpers})
    toolchain = TestToolchain(compiler, args.verilator.resolve(), work)
    report = dict(status='running', module=module, work=str(work), source_sha256=hashes, stages={},
                  scope='tagged ADC sample interface, active timing recovery, aligned supervised DSP training and PCS; physical ADC/clock and autonomous training remain separate')
    if args.case == 'autonomous_pcs':
        report['scope'] = 'autonomous echo/idle acquisition and Ethernet decoding from recovered symbol observations; ADC, timing recovery, startup handshakes and physical integration remain separate'
    if args.case == 'autonomous_phy':
        report['scope'] = 'sample timing recovery, local-reference alignment, clock crossing and autonomous DSP/PCS; electrical ADC, startup handshakes and physical integration remain separate'

    if args.case == 'autonomous_adc_phy':
        report['scope'] = 'actual SAR sequencing/comparator decisions through aperture alignment, timing recovery, clock crossing and autonomous DSP/PCS; transistor ADC, startup handshake and full-chip physical integration remain separate'

    if args.case == 'qualified_adc_phy':
        report['scope'] = 'connected reset qualification, SAR, recovery, crossing and autonomous DSP/PCS; excludes transistor ADC, reset-tree recovery/removal and full-chip physical signoff'

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (root / 'checks.json').write_text(payload)

    def command(label, argv):
        bounded = toolchain.command(argv)
        if args.memory_mib:
            bounded = ['prlimit', '--as=' + str(args.memory_mib * 1024 * 1024), '--', *bounded]
        with (work / (label + '.log')).open('w') as log:
            execute(bounded, cwd=work, env=toolchain.env, stdout=log, stderr=-2,
                    check=True, timeout=3600, memory_exclusive=False if args.memory_mib else None)

    save()
    try:
        vectors = work / 'fixtures'
        fixture_args = []
        modes = ('autonomous',) if args.case == 'autonomous_pcs' else ('asynchronous',) if args.case in ('async_phy', 'autonomous_phy', 'autonomous_adc_phy', 'qualified_adc_phy') else ('fixed', 'tracking') if is_phy else ('context',)
        if is_phy:
            report['fixture'] = (generate_autonomous_pcs if args.case in ('autonomous_pcs', 'autonomous_phy', 'autonomous_adc_phy', 'qualified_adc_phy') else generate_pcs)(vectors)
            report['fixture_sha256'] = {p.name: digest(p) for p in vectors.iterdir()}
            fixture_args = [vectors]
        def check_coverage(path):
            output = path.read_text()
            if args.case in ('async_phy', 'autonomous_phy', 'autonomous_adc_phy', 'qualified_adc_phy'):
                rows = re.findall(r'^case (\d+) master=(\d+) frames=(\d+) bytes=(\d+) events=(\d+) PASS$', output, re.M)
                expected = []
                for i, case in enumerate(report['fixture']):
                    stop = (2 * case['symbols'] + 64) * 4000
                    period, phase = (6944 if i % 2 else 7576), i * 751
                    common = math.gcd(4000, period)
                    overlap = 0
                    if phase % common == 0:
                        first = 4000 * ((phase // common) * pow(4000 // common, -1, period // common) % (period // common))
                        repeat = 4000 * period // common
                        overlap = max(0, (stop - first + repeat - 1) // repeat)
                    events = 2 + stop // 4000 + max(0, (stop - phase + period - 1) // period) - overlap
                    if args.case == 'autonomous_adc_phy':
                        events += 4103
                    elif args.case == 'qualified_adc_phy':
                        # Two complete channel epochs, an 83-event reset
                        # prelude, two 4107-edge startups and a stopped-clock pulse.
                        events = 2 * (events - 2) + 8301
                    expected.append((i, i // 2, 4 if args.case == 'qualified_adc_phy' else 2,
                                     3196 if args.case == 'qualified_adc_phy' else 1598, events))
                if [tuple(map(int, row)) for row in rows] != expected or len(output.splitlines()) != 4:
                    raise ValueError('missing asynchronous PHY frame coverage: ' + output)
            elif is_phy:
                rows = re.findall(r'^case (\d+) master=(\d+) frames=(\d+) bytes=(\d+) clocks=(\d+) PASS$', output, re.M)
                actual = [tuple(map(int, row)) for row in rows]
                expected = [(i, i // 2, 2, 1598, 2 * row['symbols'] + 2 + (32067 if args.case == 'clocked_phy' else 4167 if args.case == 'adc_phy' else 0)) for i, row in enumerate(report['fixture'])]
                if args.case == 'autonomous_pcs':
                    # Its clock-enable fixture removes the bubble at n % 97=0
                    # and restores it at n % 97=1. A final unpaired removal
                    # leaves one fewer clock, without losing a source symbol.
                    expected = [(i, i // 2, 2, 1598, 2 * row['symbols'] + 2 - int(row['symbols'] % 97 == 1))
                                for i, row in enumerate(report['fixture'])]
                if actual != expected or len(output.splitlines()) != 4:
                    raise ValueError('missing connected PHY frame coverage: ' + output)
            elif args.case == 'clock':
                if output != 'PASS clock monitor: qualification, drift, wrap, stopped clocks, power loss and restart\n':
                    raise ValueError('missing clock monitor coverage')
            elif args.case == 'capture':
                if output != 'PASS capture 209047 aperture-aligned quartets\n':
                    raise ValueError('missing aperture/metadata coverage')
            elif output != '289696 context clocks PASS\n':
                raise ValueError('missing context wrap, support, stale-query or fault coverage')
        for mode in modes:
            command('native_' + mode, [args.native.resolve(), *fixture_args, work / ('native_' + mode + '.csv'),
                                      *(['track'] if mode == 'tracking' else [])])
            check_coverage(work / ('native_' + mode + '.log'))
        report['stages']['native'] = dict(status='pass', independent_expectations=True, modes=list(modes))
        save()
        clocks = (['--primary_clock', 'adc_clk', '250000000', '--secondary_clock', 'ref_clk', '125000000']
                  if args.case in ('clock', 'clocked_phy') else
                  ['--primary_clock', 'adc_clk', '250000000', '--secondary_clock', 'pcs_clk', '144000000']
                  if args.case in ('async_phy', 'autonomous_phy', 'autonomous_adc_phy', 'qualified_adc_phy') else ['--primary_clock', 'clk', '136000000']
                  if args.case == 'autonomous_pcs' else ['--primary_clock', 'clk', '250000000'])
        if args.case == 'qualified_adc_phy':
            clocks += ['--secondary_clock', 'ref_clk', '125000000']
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                           *clocks, entry, '--',
                           '-I' + str(ANALOG.parent / 'include')])
        if any(s in (work / 'convert.log').read_text() for s in ('MISSED CALL', 'Missing register strobe', "can't find wire")):
            raise ValueError('converter reported incomplete state scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv'] + sorted(
            p for p in (work / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')
        objects = work / 'rtl_obj'
        command('rtl_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                             '-j', '1' if args.memory_mib else '2', '--top-module', module, '-Wno-fatal', '--Mdir', objects,
                             '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                             '-MAKEFLAGS', 'OPT_FAST=-O0 OPT_SLOW=-O0'])
        report['traces'] = {}
        for mode in modes:
            command('rtl_' + mode, [objects / ('V' + module), *fixture_args, work / ('rtl_' + mode + '.csv'),
                                   *(['track'] if mode == 'tracking' else [])])
            check_coverage(work / ('rtl_' + mode + '.log'))
            if (work / ('rtl_' + mode + '.log')).read_text() != (work / ('native_' + mode + '.log')).read_text():
                raise ValueError('native/RTL coverage mismatch')
            if digest(work / ('rtl_' + mode + '.csv')) != digest(work / ('native_' + mode + '.csv')):
                raise ValueError('native/RTL complete trace mismatch: ' + mode)
            report['traces'][mode] = {flow: archive(work / (flow + '_' + mode + '.csv')) for flow in ('native', 'rtl')}
        report['stages']['rtl'] = dict(status='pass', independent_expectations=True, exact_native=True, modes=list(modes))
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification input changed during run')
        report.update(status='pass', rtl_sha256={str(p): digest(p) for p in rtl})
        # Retain compact evidence; fixtures are deterministic and objects are disposable.
        shutil.rmtree(objects)
        if is_phy:
            shutil.rmtree(vectors)
        print('PASS ' + module + ': independent C++/RTL checks and exact trace comparison')
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
    parser.add_argument('--memory-mib', type=int, choices=range(256, 1537), default=0,
                        help='bound each serial process instead of reserving the shared large-memory slot')
    parser.add_argument('--case', choices=('context', 'phy', 'capture', 'adc_phy', 'clock', 'clocked_phy', 'async_phy', 'autonomous_pcs', 'autonomous_phy', 'autonomous_adc_phy', 'qualified_adc_phy'), required=True)
    run(parser.parse_args())
