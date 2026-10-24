#!/usr/bin/env python3
"""Verify startup sequencing in native, RTL and optional mapped IHP gates."""
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
from hardware import configure_cleanup, execute, source_dependencies


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    fixture, entry = SOURCE.with_suffix('.cpp'), ANALOG / '1G/adc_startup.cpp'
    paths = [SOURCE, args.native.resolve(), args.converter.resolve(), args.verilator.resolve()]
    if args.tools:
        binary = args.tools.resolve() / 'bin'
        liberty = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
        paths += [binary / name for name in ('yosys', 'abc', 'sv2v')] + [liberty]
    hashes = source_dependencies([fixture, entry], ANALOG.parent, args.cxx)
    hashes.update({str(p): digest(p) for p in paths})
    report = dict(status='running', work=str(work), source_sha256=hashes, stages={},
        scope='startup digital sequencing only: 4096 precharge and 4 release clocks, abort/restart; electrical charge settling, physical reset and supplies require coupled circuit tests')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(label, argv, output=None):
        with (output or work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, env=os.environ, stdout=log, stderr=-2, check=True, timeout=900)

    def simulate(stage, sources):
        objects = work / (stage + '_obj')
        command(stage + '_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
            '-j', '1', '--top-module', 'ADCStartupSequencer', '-Wno-fatal', '--Mdir', objects,
            '-CFLAGS', '-DVERILATOR -std=c++17', *sources, fixture,
            '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        command(stage + '_simulate', [objects / 'VADCStartupSequencer', work / (stage + '.csv')])
        if (work / 'native.csv').read_bytes() != (work / (stage + '.csv')).read_bytes():
            raise ValueError('startup ' + stage + ' trajectory differs from independently checked native outputs')
        report['stages'][stage] = dict(status='pass', checked_clocks=40000, trace_sha256=digest(work / (stage + '.csv')))
        save()

    save()
    try:
        command('native', [args.native.resolve(), work / 'native.csv'])
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl', entry,
            '--', '-I' + str(ANALOG.parent / 'include')])
        rtl = [work / 'rtl' / name for name in ('Predef_pkg.sv', 'ADCStartupSequencer.sv')]
        simulate('rtl', rtl)
        if args.tools:
            command('sv2v', [binary / 'sv2v', *rtl], work / 'design.v')
            if any(c in str(work) + str(liberty) + str(binary) for c in ('"', '\\', '\n')):
                raise ValueError('unsupported synthesis path')
            (work / 'synth.ys').write_text(f'''read_liberty -lib "{liberty}"
read_verilog "{work / 'design.v'}"
hierarchy -check -top ADCStartupSequencer
synth -top ADCStartupSequencer -flatten -noabc -noshare
dfflibmap -liberty "{liberty}"
abc -exe "{binary / 'abc'}" -liberty "{liberty}"
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr "{work / 'mapped.v'}"
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir "{liberty}"
write_verilog -noattr "{work / 'cells.v'}"
''')
            command('synthesis', [binary / 'yosys', '-Q', '-T', '-s', work / 'synth.ys'])
            simulate('mapped', [work / 'mapped.v', work / 'cells.v'])
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('startup verification inputs changed')
        report.update(status='pass', rtl_sha256={str(p): digest(p) for p in rtl},
            native_trace_sha256=digest(work / 'native.csv'),
            artifact_sha256={str(p): digest(p) for p in work.iterdir() if p.is_file() and p.name != 'checks.json'})
        print('PASS 40,000 independent startup clocks in', ', '.join(report['stages']))
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
    parser.add_argument('--tools', type=Path)
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    if bool(args.tools) != bool(args.pdk):
        parser.error('IHP mapping needs both --tools and --pdk')
    run(args)
