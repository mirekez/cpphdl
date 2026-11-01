#!/usr/bin/env python3
"""Check asynchronous assertion and domain-local release in native/RTL/IHP cells."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

from hardware import configure_cleanup, execute, source_dependencies

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    stem = 'phy_reset_control' if args.control else 'phy_reset_release'
    top = 'PHYResetControl' if args.control else 'PHYResetRelease'
    events = 153600 if args.control else 30720
    schedules = 10240 if args.control else 2048
    entry = SOURCE.parent.parent / (stem + '.cpp')
    fixture = SOURCE.with_name(stem + '.cpp')
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    tools = args.tools.resolve() / 'bin'
    hashes = source_dependencies([entry, fixture], ANALOG.parent, args.cxx)
    inputs = [SOURCE, SOURCE.with_name('hardware.py'), ANALOG.parent / 'tests/TestToolchain.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', library, args.native.resolve(),
        args.converter.resolve(), args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin'),
        Path(args.cxx).resolve(), *[tools / name for name in ('sv2v', 'yosys', 'abc')]]
    hashes.update({str(p): digest(p) for p in inputs})
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='functional asynchronous assertion / two-edge release in three domains; excludes metastability MTBF, reset routing and recovery/removal signoff')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (work / 'hardware_checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv, bounded=True):
        # This six-register module can run beside the receiver without taking
        # its large-memory slot. Bound each compiler and mapper to 1 GiB.
        if bounded:
            argv = ['prlimit', '--as=1073741824', '--', *map(str, argv)]
        with (work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, stdout=log, stderr=-2,
                    timeout=300, check=True, memory_exclusive=False)

    def build(flow, sources):
        objects = work / (flow + '_obj')
        command(flow + '_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '-j', '1',
            '--top-module', top, '-Wno-fatal', '--Mdir', objects,
            '-CFLAGS', '-DVERILATOR -std=c++17', *sources, fixture,
            '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command(flow, [objects / ('V' + top), work / (flow + '.csv')])

    save()
    try:
        command('native', [args.native.resolve(), work / 'native.csv'])
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
            '--primary_clock', 'adc_clk', '250000000', '--secondary_clock', 'pcs_clk', '136000000',
            '--secondary_clock', 'ref_clk', '125000000', entry, '--', '-I' + str(ANALOG.parent / 'include')])
        rtl = [work / 'rtl/Predef_pkg.sv', *sorted(p for p in (work / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')]
        source = (work / 'rtl/PHYResetRelease.sv').read_text()
        if source.count('(* ASYNC_REG = "TRUE" *)') != 6 or any(
                'always_ff @(posedge ' + clock + ' or posedge reset)' not in source
                for clock in ('adc_clk', 'pcs_clk', 'ref_clk')):
            raise ValueError('missing asynchronous reset or synchronizer attributes')
        if args.control and '.reset(reset || withdraw_comb)' not in (work / 'rtl/PHYResetControl.sv').read_text():
            raise ValueError('converter discarded clock qualification from child reset')
        build('rtl', rtl)
        with (work / 'design.v').open('w') as output:
            execute([str(tools / 'sv2v'), '+RTS', '-N1', '-M256m', '-RTS', *map(str, rtl)],
                    stdout=output, check=True, timeout=60, memory_exclusive=False)
        synth = work / 'synth.ys'
        if any(any(c in str(p) for c in ' \n"{}\\') for p in (work, library, tools)):
            raise ValueError('unsupported synthesis path')
        synth.write_text(f'''read_liberty -lib {library}
read_verilog {work}/design.v
hierarchy -check -top {top}
synth -top {top} -flatten -noabc -noshare
dfflibmap -liberty {library}
abc -exe {tools}/abc -liberty {library}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr {work}/mapped.v
write_json {work}/mapped.json
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {library}
write_verilog -noattr {work}/cells.v
''')
        command('synthesis', [tools / 'yosys', '-Q', '-T', '-s', synth])
        cells = json.loads((work / 'mapped.json').read_text())['modules'][top]['cells']
        inventory = Counter(c['type'] for c in cells.values())
        if (sum(inventory[name] for name in ('sg13g2_dfrbp_1', 'sg13g2_dfrbpq_1')) != 6 or len(cells) > 32
                or any(not name.startswith('sg13g2_') for name in inventory)):
            raise ValueError('missing physical asynchronous-reset registers or unexpected mapping')
        build('mapped', [work / 'cells.v', work / 'mapped.v'])
        expected = f'PASS reset release: {schedules} stop/start schedules; {events} events\n'
        traces = {}
        for flow in ('native', 'rtl', 'mapped'):
            if (work / (flow + '.log')).read_text() != expected:
                raise ValueError('incomplete reset release contract: ' + flow)
            path = work / (flow + '.csv')
            count = 0
            with path.open() as stream:
                for count, row in enumerate(stream, 1):
                    values = row.strip().split(',')
                    if len(values) != 6 or int(values[0]) != count - 1:
                        raise ValueError('incomplete reset trajectory')
            if count != events:
                raise ValueError('missing reset events')
            traces[flow] = digest(path)
        if len(set(traces.values())) != 1:
            raise ValueError('native/RTL/IHP reset trajectories differ')
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('reset qualification sources changed')
        # The physical flow replays this exact native trajectory after routing.
        shutil.copyfile(work / 'native.csv', work / 'cpp_trace.csv')
        report.update(status='pass', module=top, schedules=schedules, events=events, cell_inventory=dict(inventory),
            native_rtl_exact=True, native_mapped_exact=True, mapped_sha256=digest(work / 'mapped.v'),
            trace_sha256=traces, artifact_sha256={str(p): digest(p) for p in [*rtl, work / 'mapped.v', work / 'mapped.json']})
        print(f'PASS {top}: native/RTL/IHP cells, {events} exact events')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native', 'converter', 'verilator', 'tools', 'pdk', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--control', action='store_true', help='include enable, power and both clock qualifications')
    run(parser.parse_args())
