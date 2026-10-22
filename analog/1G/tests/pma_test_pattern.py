#!/usr/bin/env python3
"""Check the actual PMA test-symbol class against independent sequence vectors."""
import argparse
from collections import Counter
import filecmp
import hashlib
import json
from pathlib import Path
import sys
import tempfile

from pma_test_pattern_reference import generate
from hardware import configure_cleanup, execute, source_dependencies

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
TOP = 'PMATestPattern'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args, *, source=SOURCE, top=TOP, generate_vectors=generate,
        reference=SOURCE.with_name('pma_test_pattern_reference.py'), clock_hz=125000000,
        scope='exact test-symbol sequences and control behavior; no physical clock selection or MDI compliance proof'):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    module_source = source.parents[1] / (source.stem + '.cpp')
    hashes = source_dependencies([source.with_suffix('.cpp'), module_source],
                                 ANALOG.parent, args.cxx)
    tracked = [SOURCE, source, reference, SOURCE.with_name('pma_test_pattern_reference.py'), args.bench.resolve()]
    if args.flow != 'cpp':
        tracked += [args.converter.resolve(), args.verilator.resolve()]
    if args.flow == 'mapped':
        liberty = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
        tracked += [liberty] + [args.tools.resolve() / 'bin' / name for name in ('yosys', 'sv2v', 'abc')]
        tracked += [args.tools.resolve() / name for name in (
            'root/usr/bin/yosys', 'sv2v-Linux/sv2v', 'root/usr/bin/berkeley-abc')]
    hashes.update({str(path): digest(path) for path in tracked})
    report = dict(status='running', flow=args.flow, run_directory=str(work), source_sha256=hashes,
        clock_hz=clock_hz, scope=scope)

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(label, argv, timeout=300):
        with (work / (label + '.log')).open('w') as output:
            execute(list(map(str, argv)), stdout=output, stderr=-2, timeout=timeout, check=True)

    save()
    try:
        fixture = work / 'vectors.txt'
        report['checked_clocks'] = generate_vectors(fixture)
        command('native', [args.bench.resolve(), fixture, work / 'native.csv'])
        if args.flow != 'cpp':
            rtl = work / 'rtl'
            command('convert', [args.converter.resolve(), '--primary_clock', 'clk', str(clock_hz),
                '--generated-dir', rtl, module_source, '--',
                '-I' + str(ANALOG.parent / 'include')])
            design = [rtl / 'Predef_pkg.sv'] + sorted(path for path in rtl.glob('*.sv') if path.name != 'Predef_pkg.sv')
            defines = '-DVERILATOR -std=c++17'
            if args.flow == 'mapped':
                with (work / 'design.v').open('w') as output, (work / 'sv2v.log').open('w') as diagnostics:
                    execute([str(args.tools.resolve() / 'bin/sv2v'), *map(str, design)],
                            stdout=output, stderr=diagnostics, check=True, timeout=180)
                script = f'''read_liberty -lib "{liberty}"
read_verilog "{work / 'design.v'}"
hierarchy -check -top {top}
synth -top {top} -flatten -noabc
 dfflibmap -liberty "{liberty}"
abc -exe "{args.tools.resolve() / 'bin/abc'}" -liberty "{liberty}"
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr "{work / 'mapped.v'}"
write_json "{work / 'mapped.json'}"
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir "{liberty}"
write_verilog -noattr "{work / 'cells.v'}"
'''
                (work / 'synth.ys').write_text(script)
                command('synthesis', [args.tools.resolve() / 'bin/yosys', '-s', work / 'synth.ys'])
                module = json.loads((work / 'mapped.json').read_text())['modules'][top]
                counts = Counter(cell['type'] for cell in module['cells'].values())
                if not counts or any(not name.startswith('sg13g2_') for name in counts):
                    raise ValueError('test-pattern mapping contains non-IHP cells')
                report['cells'] = dict(counts)
                design = [work / 'mapped.v', work / 'cells.v']
                defines += ' -DMAPPED'
            command('build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '1', '--top-module', top, '-Wno-fatal', '--Mdir', work / 'obj',
                '-CFLAGS', defines, *design, source.with_suffix('.cpp'),
                '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx], timeout=600)
            command('simulate', [work / 'obj' / ('V' + top), fixture, work / 'hardware.csv'])
            if not filecmp.cmp(work / 'native.csv', work / 'hardware.csv', shallow=False):
                raise ValueError('test-pattern native/hardware trajectories differ')
            report['native_hardware_exact'] = True
        if any(digest(path) != expected for path, expected in hashes.items()):
            raise ValueError('test-pattern sources/tools changed during verification')
        report['artifact_sha256'] = {str(path): digest(path) for path in work.rglob('*')
            if path.is_file() and (path.suffix in ('.csv', '.txt', '.sv', '.v') or path.name == 'mapped.json')}
        report['status'] = 'pass'
        print('PASS', top, args.flow, report['checked_clocks'], 'independently checked clocks')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'rtl', 'mapped'), required=True)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--converter', type=Path)
    parser.add_argument('--verilator', type=Path)
    parser.add_argument('--tools', type=Path)
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    if args.flow != 'cpp' and (args.converter is None or args.verilator is None):
        parser.error('hardware flows require the converter and Verilator')
    if args.flow == 'mapped' and (args.tools is None or args.pdk is None):
        parser.error('mapped flow requires the prepared PDK and synthesis tools')
    return args


if __name__ == '__main__':
    configure_cleanup()
    run(arguments())
