#!/usr/bin/env python3
"""Check 250 MHz slow-cell timing and the repaired receive controller/calibrator."""
import argparse
from collections import Counter
import filecmp
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
from types import SimpleNamespace

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / '1G/IHPSG13G2'))
from hardware import TestToolchain, configure_cleanup, execute
import cell_timing
from mapped_area import library_areas


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    cases = {'ReceiveAnalogGainControl': ('receive_gain_control', 200000, 'receive_gain_control_reference.py'),
             'ReceiveRangeCalibration': ('receive_range_calibration', 50000, 'receive_range_calibration_reference.py'),
             'ReceiveAnalogAcquisition': ('receive_acquisition', 400000, 'receive_gain_control_reference.py')}
    stem, minimum_clocks, reference_name = cases[args.module]
    report = dict(module=args.module, status='running', run_directory=str(work), period_ns=4,
        scope='slow-corner cell timing and repaired-netlist functional proof; no routed timing or physical qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    save()
    try:
        proof = args.mapped_proof.resolve()
        evidence = json.loads(proof.read_text())
        if (evidence.get('status') != 'pass' or evidence.get('flow') != 'mapped'
                or not evidence.get('native_hardware_exact') or evidence.get('clock_hz') != 250000000
                or evidence.get('checked_clocks', 0) < minimum_clocks):
            raise ValueError('missing or failed 250 MHz gain-controller functional evidence')
        bench = SOURCE.with_name(stem + '.cpp')
        reference = SOURCE.with_name(reference_name)
        for path in (bench, reference):
            if evidence.get('source_sha256', {}).get(str(path)) != digest(path):
                raise ValueError('missing or stale gain-controller checker provenance')
        hashes = {**evidence['source_sha256'], **evidence['artifact_sha256']}
        hashes.update({str(proof): digest(proof), str(SOURCE): digest(SOURCE),
            str(SOURCE.with_name('cell_timing.py')): digest(SOURCE.with_name('cell_timing.py')),
            str(ANALOG / '1G/IHPSG13G2/mapped_area.py'): digest(ANALOG / '1G/IHPSG13G2/mapped_area.py')})
        for path, expected in hashes.items():
            if digest(path) != expected:
                raise ValueError('changed mapped gain-controller input: ' + path)
        mapped = Path(evidence['run_directory'])
        for name in ('mapped.v', 'cells.v', 'vectors.txt', 'native.csv'):
            shutil.copyfile(mapped / name, work / name)
        module = json.loads((mapped / 'mapped.json').read_text())['modules']
        tops = {name for name, contents in module.items()
                if not int(str(contents.get('attributes', {}).get('blackbox', '0')), 2)}
        if tops != {args.module}:
            raise ValueError('mapped functional proof describes a different module')
        for path in (args.tools.resolve() / 'bin/openroad', args.tools.resolve() / 'root/usr/bin/openroad',
                     args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin'), Path(args.cxx).resolve()):
            hashes[str(path)] = digest(path)
        report['input_sha256'] = hashes
        report['checked_clocks'] = evidence['checked_clocks']
        save()
        timing_args = SimpleNamespace(work=work, pdk=args.pdk, tools=args.tools,
            module=args.module, period=4, repair=True, require_functional=False)
        if cell_timing.run(timing_args) != 0:
            raise ValueError('gain controller fails repaired slow-cell timing at 250 MHz')
        timing = json.loads((work / 'cell_timing.json').read_text())
        report['cell_timing'] = timing
        hashes.update(timing['input_sha256'])
        hashes[str(work / 'resized.v')] = digest(work / 'resized.v')
        with (work / 'build.log').open('w') as output:
            execute(toolchain.command([str(args.verilator.resolve()), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '1', '--top-module', args.module, '-Wno-fatal', '--Mdir', str(work / 'obj'),
                '-CFLAGS', '-DVERILATOR -DMAPPED -std=c++17', str(work / 'resized.v'), str(work / 'cells.v'),
                str(bench)]), env=toolchain.env,
                stdout=output, stderr=-2, check=True, timeout=600)
        with (work / 'simulate.log').open('w') as output:
            execute([str(work / ('obj/V' + args.module)), str(work / 'vectors.txt'), str(work / 'resized.csv')],
                    env=toolchain.env, stdout=output, stderr=-2, check=True, timeout=120)
        if not filecmp.cmp(work / 'native.csv', work / 'resized.csv', shallow=False):
            raise ValueError('gain-controller repaired/native trajectories differ')
        report['native_repaired_exact'] = True
        liberty = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
        hashes[str(liberty)] = digest(liberty)
        areas = library_areas(liberty.read_text())
        cells = Counter(re.findall(r'^\s+(\w+)\s+(?:\\\S+|\w+)\s*\(', (work / 'resized.v').read_text(), re.M))
        if not cells or set(cells) - set(areas):
            raise ValueError('timing repair contains unknown physical masters')
        report['repaired_cell_count'] = sum(cells.values())
        report['repaired_cell_area_um2'] = sum(areas[name] * count for name, count in cells.items())
        report['artifact_sha256'] = {name: digest(work / name) for name in
            ('resized.v', 'resized.csv', 'cell_timing.json', 'cell_timing.log')}
        for path, expected in hashes.items():
            if digest(path) != expected:
                raise ValueError('gain-controller timing inputs changed during verification: ' + path)
        report['status'] = 'pass'
        print('PASS', report['checked_clocks'], 'repaired gate clocks, setup', timing['setup_slack_ns'], 'ns', flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', choices=('ReceiveAnalogGainControl', 'ReceiveRangeCalibration', 'ReceiveAnalogAcquisition'), default='ReceiveAnalogGainControl')
    parser.add_argument('--mapped-proof', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--verilator', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
