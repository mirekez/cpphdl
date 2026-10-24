#!/usr/bin/env python3
"""Place, route and verify the minimum safe SAR controller with the IHP PDK."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path[:0] = [str(SOURCE.parent), str(ANALOG / '1G/tests')]
from hardware import configure_cleanup, execute
from mapped_area import lef_areas
from physical_module import implement


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare(args, root):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'pass' or proof.get('cases', {}).get('13', {}).get('status') != 'pass':
        raise ValueError('independently checked compact-bank mapped proof required')
    previous = Path(proof['work']) / '13'
    case = proof['cases']['13']
    for name, key in (('mapped.v', 'mapped_sha256'), ('mapped_interface.sv', 'interface_sha256'), ('mapped_trace.csv', 'trace_sha256')):
        if digest(previous / name) != case[key]:
            raise ValueError('changed mapped bank artifact: ' + name)
    hashes = dict(proof['source_sha256'])
    hashes.update({str(p): digest(p) for p in (SOURCE, args.proof.resolve(), previous / 'mapped.v',
        previous / 'mapped.json', previous / 'mapped_interface.sv', previous / 'mapped_trace.csv',
        SOURCE.parent / 'physical_module.py', ANALOG / '1G/tests/adc_bank.cpp')})
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('stale compact-bank source/tool provenance')
    module = json.loads((previous / 'mapped.json').read_text())['modules']['InterleavedSARBankController']
    cells = Counter(c['type'] for c in module['cells'].values())
    if dict(cells) != case['cells'] or sum(cells.values()) != case['cell_count']:
        raise ValueError('compact-bank database differs from checked mapped inventory')
    ports = {'clk': 1, 'reset': 1, 'enable_in': 1, 'sample_enable_in': 1, 'comparisons_in': 13,
        'trial_out': 104, 'acquire_out': 13, 'convert_out': 13, 'data_out': 8, 'slice_out': 4,
        'valid_out': 1, 'fault_out': 1}
    if {name: len(port['bits']) for name, port in module['ports'].items()} != ports:
        raise ValueError('compact bank physical port contract changed')
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    liberty = library / 'lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    footprints = lef_areas((library / 'lef/sg13g2_stdcell.lef').read_text())
    if abs(sum(footprints[c] * number for c, number in cells.items()) - case['cell_area_um2']) > 1e-6:
        raise ValueError('compact bank cell footprint differs from mapped proof')
    work = root / 'mapped'
    work.mkdir()
    for name in ('mapped.v', 'mapped.json', 'mapped_interface.sv'):
        shutil.copyfile(previous / name, work / name)
    # Recreate the simulation cell definitions from the pinned Liberty, then
    # rerun the independent harness before adopting a physical input proof.
    if any(c in str(liberty) + str(work) for c in ('"', '\\', '\n')):
        raise ValueError('unsupported synthesis path')
    (work / 'cells.ys').write_text(f'read_liberty -ignore_miss_func -ignore_miss_dir "{liberty}"\n'
        f'write_verilog -noattr "{work / "cells.v"}"\n')
    with (work / 'cells.log').open('w') as log:
        execute([str(args.tools.resolve() / 'bin/yosys'), '-Q', '-T', '-s', str(work / 'cells.ys')],
            cwd=work, stdout=log, stderr=-2, check=True, timeout=60)
    text = (work / 'mapped.v').read_text()
    token = 'module InterleavedSARBankController('
    if text.count(token) != 1:
        raise ValueError('missing compact-bank mapped top')
    (work / 'mapped_flat.v').write_text(text.replace(token, 'module InterleavedSARBankController_mapped(', 1))
    with (work / 'build.log').open('w') as log:
        execute([str(args.verilator.resolve()), '--cc', '--exe', '--build', '--no-skip-identical', '--flatten',
            '--output-groups', '0', '-j', '1', '--top-module', 'InterleavedSARBankController', '-Wno-fatal',
            '--Mdir', str(work / 'obj'), '-CFLAGS', '-DVERILATOR -DBANK_SLICES=13 -std=c++17',
            str(work / 'mapped_flat.v'), str(work / 'mapped_interface.sv'), str(work / 'cells.v'),
            str(ANALOG / '1G/tests/adc_bank.cpp'), '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx],
            cwd=work, stdout=log, stderr=-2, check=True, timeout=900)
    with (work / 'simulate.log').open('w') as log:
        execute([str(work / 'obj/VInterleavedSARBankController'), '13', str(work / 'cpp_trace.csv')],
            cwd=work, stdout=log, stderr=-2, check=True, timeout=60)
    if digest(work / 'cpp_trace.csv') != case['trace_sha256']:
        raise ValueError('fresh Liberty-cell trajectory differs from the independently checked mapped/native trace')
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('compact-bank preparation inputs changed')
    (work / 'hardware_checks.json').write_text(json.dumps(dict(status='pass', module='InterleavedSARBankController',
        native_mapped_exact=True, checked_clocks=8256, mapped_sha256=digest(work / 'mapped.v'), source_sha256=hashes,
        simulation_interface_sha256=digest(work / 'mapped_interface.sv'),
        scope='thirteen-slice digital SAR controller; independent continuous/gapped expected codes, identities, cadence and controls; fresh pinned-Liberty gate recheck'), indent=2) + '\n')
    return work


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'tools', 'pdk', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--die-size', type=float)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    report = dict(status='running', work=str(work), scope='standalone compact SAR controller; not analog ADC/PHY/chip qualification')
    try:
        mapped = prepare(args, work)
        report['mapped'] = str(mapped)
        (root / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.prepare_only:
            report['status'] = 'prepared'
        else:
            physical = work / 'physical'
            options = SimpleNamespace(module='InterleavedSARBankController', work=physical, mapped_work=mapped,
                pdk=args.pdk, tools=args.tools, verilator=str(args.verilator.resolve()), cxx=args.cxx,
                workers=1, route_threads=1, max_transition=.5, die_size=args.die_size)
            result = implement(options)
            report['physical_report'] = str(physical / 'physical_module.json')
            if result != 0:
                raise ValueError('compact SAR bank physical checks failed')
            report['status'] = 'component_checks_pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (root / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
