#!/usr/bin/env python3
"""Check shared-node partitioning against native equations and flat Verilog-A."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path[:0] = [str(ANALOG / 'VerilogA'), str(ANALOG / '1G/tests')]
from compare import checked_rows, errors, read_cpp, read_spice
from hardware import configure_cleanup, execute, source_dependencies

HEADER = ['time_s', 'Y', 'Z']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def absolute(rows):
    checked_rows(rows, 3, 'coupled RC')
    if rows[0][0] > 1e-8 or rows[-1][0] < 9.99e-6:
        raise ValueError('coupled RC trace is truncated')
    peak = 0
    for time, y, z in rows:
        expected_z = .5 * (1 - math.exp(-time / 1.5e-6))
        expected_y = 2 / 3 + expected_z / 3
        peak = max(peak, abs(y - expected_y), abs(z - expected_z))
    if peak > .001:
        raise ValueError('coupled RC absolute error exceeds 1 mV')
    return peak


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    tracked = [SOURCE, SOURCE.with_name('hierarchy.cpp'), ANALOG / 'VerilogA/hierarchy.py',
               ANALOG / 'VerilogA/Driver.cpp', ANALOG / 'VerilogA/compare.py',
               args.bench.resolve(), args.driver.resolve(), args.openvaf.resolve(), args.ngspice.resolve()]
    hashes = source_dependencies([SOURCE.with_name('hierarchy.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', run_directory=str(work), source_sha256=hashes,
                  scope='same coupled RC graph in native, flat Verilog-A, partitioned experiment and partitioned reusable IC')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(label, argv, cwd=work, timeout=180):
        with (work / (label + '.log')).open('w') as output:
            # These fixed RC leaves compile with a small memory footprint.
            # They can run while a large physical check owns the EDA slot.
            execute(list(map(str, argv)), cwd=cwd, stdout=output, stderr=-2, check=True, timeout=timeout,
                    memory_exclusive=False if Path(str(argv[0])).name.startswith('openvaf') else None)

    def compile_models(label, directory, module):
        metadata = json.loads((directory / (module + '.hierarchy.json')).read_text())
        for index, master in enumerate(metadata['masters']):
            command(label + '-compile-' + str(index), [args.openvaf.resolve(), directory / (master + '.va'),
                                                       '-o', directory / (master + '.osdi')])
        return metadata

    save()
    try:
        flat, divided, bare = work / 'flat', work / 'divided', work / 'bare'
        command('native', [args.bench.resolve(), flat])
        header, native = read_cpp(flat / 'cpp/trace.csv')
        if header != HEADER:
            raise ValueError('changed coupled RC observations')
        command('flat-compile', [args.openvaf.resolve(), flat / 'design.va', '-o', flat / 'design.osdi'])
        command('flat-simulate', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'], cwd=flat)
        command('partition-experiment', [args.driver.resolve(), '--verilog-a', '--partition', flat / 'design.va',
                                        '--output', divided, '--experiment', flat / 'bench.cir'])
        report['partitioned_experiment'] = compile_models('divided', divided, 'AnalogExperiment')
        command('divided-simulate', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'], cwd=divided)
        command('export-reusable', [args.driver.resolve(), SOURCE.with_name('hierarchy.cpp'), '--top', 'CoupledRC',
            '--ports', 'Vss,IN,Y,Z', '--module', 'CoupledRC', '--output', work / 'CoupledRC.va', '--cxx', args.cxx], timeout=600)
        command('partition-reusable', [args.driver.resolve(), '--partition', work / 'CoupledRC.va', '--output', bare])
        metadata = compile_models('bare', bare, 'CoupledRC')
        loads = '\n'.join('pre_osdi ' + master + '.osdi' for master in metadata['masters'])
        (bare / 'bench.cir').write_text('''Coupled reusable IC
.include "''' + str(bare / metadata['circuit_file']) + '''"
Vsource input 0 1
Xdut 0 input y z CoupledRC
.ic V(z)=0
.control
''' + loads + '''
set noaskquit
set wr_singlescale
set wr_vecnames
set numdgt=17
save v(y) v(z)
tran 10n 10u 0 5n uic
wrdata spice.dat v(y) v(z)
quit
.endc
.end
''')
        command('bare-simulate', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'], cwd=bare)
        waves = dict(native=native, flat=read_spice(flat / 'spice.dat', HEADER),
                     divided=read_spice(divided / 'spice.dat', HEADER), bare=read_spice(bare / 'spice.dat', HEADER))
        report['absolute_error_V'] = {flow: absolute(rows) for flow, rows in waves.items()}
        report['peak_differences_V'] = {flow: dict(zip(HEADER[1:], errors(rows, waves['flat'])))
                                        for flow, rows in waves.items() if flow != 'flat'}
        for flow, differences in report['peak_differences_V'].items():
            limit = .001 if flow == 'native' else .00001
            if max(differences.values()) > limit:
                raise ValueError('coupled RC comparison failed for ' + flow)
        for path, expected in hashes.items():
            if digest(path) != expected:
                raise ValueError('partition test inputs changed: ' + path)
        report['artifact_sha256'] = {str(path.relative_to(work)): digest(path) for path in work.rglob('*')
            if path.is_file() and path.suffix in ('.va', '.cir', '.csv', '.dat', '.osdi')}
        report['status'] = 'pass'
        print('PASS independent analytic checks and native/flat/partitioned waveform comparisons')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--openvaf', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
