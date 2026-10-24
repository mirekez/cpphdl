#!/usr/bin/env python3
"""Implement the initialized thirteen-slice SAR/normalizer controller physically."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path[:0] = [str(SOURCE.parent), str(ANALOG / '1G/tests')]
from adc_bank_physical import digest
from hardware import configure_cleanup, execute
from physical_module import implement

TOP = 'ADCReceiveLaneController'
PORTS = dict(clk=1, reset=1, enable_in=1, sample_enable_in=1, comparisons_in=16,
    offset_in=8, gain_in=16, trial_out=128, acquire_out=16, convert_out=16,
    data_out=8, slice_out=4, valid_out=1, fault_out=1, normalized_out=8,
    normalized_valid_out=1, normalized_slice_out=4, clipped_out=1,
    precharge_out=1, clear_out=1, startup_ready_out=1, evaluate_enable_out=1)


def checked_proof(path):
    proof = json.loads(path.read_text())
    if proof.get('status') != 'pass' or any(proof.get('stages', {}).get(stage, {}).get('status') != 'pass'
            for stage in ('rtl', 'mapped')):
        raise ValueError('passing native/RTL/mapped initialized-lane proof required')
    previous = Path(proof['work'])
    hashes = dict(proof.get('source_sha256', {}))
    if not hashes or any(digest(Path(p)) != value for p, value in hashes.items()):
        raise ValueError('stale initialized-lane source/tool evidence')
    for name in ('mapped.v', 'mapped.json', 'mapped_interface.sv', 'cells.v', 'native.csv', 'mapped.csv'):
        artifact = previous / name
        if proof.get('artifact_sha256', {}).get(str(artifact)) != digest(artifact):
            raise ValueError('changed initialized-lane artifact: ' + name)
    expected = proof.get('native_trace_sha256')
    if (not expected or digest(previous / 'native.csv') != expected or
            digest(previous / 'mapped.csv') != expected or
            any(proof['stages'][stage].get('trace_sha256') != expected or
                proof['stages'][stage].get('checked_clocks') != 15468 for stage in ('rtl', 'mapped')) or
            len((previous / 'native.csv').read_bytes().splitlines()) != 15468):
        raise ValueError('incomplete initialized-lane trajectory evidence')
    module = json.loads((previous / 'mapped.json').read_text())['modules'][TOP]
    if {name: len(port['bits']) for name, port in module['ports'].items()} != PORTS:
        raise ValueError('initialized-lane physical port contract changed')
    return previous, hashes


def prepare(args, root):
    previous, hashes = checked_proof(args.proof.resolve())
    work = root / 'mapped'
    work.mkdir()
    paths = [SOURCE, SOURCE.with_name('adc_bank_physical.py'), SOURCE.with_name('physical_module.py'), args.proof.resolve(),
        ANALOG / '1G/tests/adc_receive_lane.cpp']
    paths += [previous / name for name in ('mapped.v', 'mapped.json', 'mapped_interface.sv', 'native.csv')]
    hashes.update({str(p): digest(p) for p in paths})
    for name in ('mapped.v', 'mapped.json', 'mapped_interface.sv'):
        shutil.copyfile(previous / name, work / name)
    shutil.copyfile(previous / 'native.csv', work / 'cpp_trace.csv')
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    liberty = library / 'lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    if any(c in str(liberty) + str(work) for c in ('"', '\\', '\n')):
        raise ValueError('unsupported synthesis path')
    (work / 'cells.ys').write_text(f'read_liberty -ignore_miss_func -ignore_miss_dir "{liberty}"\n'
        f'write_verilog -noattr "{work / "cells.v"}"\n')
    with (work / 'cells.log').open('w') as log:
        execute([str(args.tools.resolve() / 'bin/yosys'), '-Q', '-T', '-s', str(work / 'cells.ys')],
            cwd=work, stdout=log, stderr=-2, check=True, timeout=60)
    text = (work / 'mapped.v').read_text()
    token = 'module ' + TOP + '('
    if text.count(token) != 1:
        raise ValueError('missing initialized-lane mapped top')
    (work / 'mapped_flat.v').write_text(text.replace(token, 'module ' + TOP + '_mapped(', 1))
    with (work / 'build.log').open('w') as log:
        execute([str(args.verilator.resolve()), '--cc', '--exe', '--build', '--no-skip-identical', '--flatten',
            '--output-groups', '0', '-j', '1', '--top-module', TOP, '-Wno-fatal', '--Mdir', str(work / 'obj'),
            '-CFLAGS', '-DVERILATOR -std=c++17', str(work / 'mapped_flat.v'), str(work / 'mapped_interface.sv'),
            str(work / 'cells.v'), str(ANALOG / '1G/tests/adc_receive_lane.cpp'), '-MAKEFLAGS',
            'CXX=' + args.cxx + ' LINK=' + args.cxx], cwd=work, stdout=log, stderr=-2, check=True, timeout=900)
    with (work / 'simulate.log').open('w') as log:
        execute([str(work / ('obj/V' + TOP)), str(work / 'mapped_trace.csv')], cwd=work,
            stdout=log, stderr=-2, check=True, timeout=60)
    if digest(work / 'mapped_trace.csv') != digest(work / 'cpp_trace.csv'):
        raise ValueError('fresh Liberty-cell lane trajectory differs from native proof')
    if any(digest(Path(p)) != value for p, value in hashes.items()):
        raise ValueError('initialized-lane preparation inputs changed')
    (work / 'hardware_checks.json').write_text(json.dumps(dict(status='pass', module=TOP,
        native_mapped_exact=True, checked_clocks=15468, mapped_sha256=digest(work / 'mapped.v'),
        source_sha256=hashes, simulation_interface_sha256=digest(work / 'mapped_interface.sv'),
        scope='initialized thirteen-slice controller plus normalization, tags and flushes; fresh pinned-Liberty recheck; no analog circuit qualification'), indent=2) + '\n')
    return work


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'tools', 'pdk', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--die-size', type=float, default=500)
    parser.add_argument('--prepare-only', action='store_true')
    resume = parser.add_mutually_exclusive_group()
    resume.add_argument('--resume-placement', type=Path,
        help='reuse checked placement; repeat routing, extraction, timing, gate replay, DRC and LVS')
    resume.add_argument('--resume-routed-database', type=Path,
        help='reuse a routed OpenDB checkpoint; repeat extraction, timing, gate replay, fill, DRC and LVS')
    args = parser.parse_args()
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    report = dict(status='running', work=str(work), scope='initialized digital receiver controller; not an analog lane/PHY/chip')
    try:
        mapped = prepare(args, work)
        report['mapped'] = str(mapped)
        if args.prepare_only:
            report['status'] = 'prepared'
        else:
            physical = work / 'physical'
            report['physical_report'] = str(physical / 'physical_module.json')
            options = SimpleNamespace(module=TOP, work=physical, mapped_work=mapped, pdk=args.pdk,
                tools=args.tools, verilator=str(args.verilator.resolve()), cxx=args.cxx,
                workers=1, route_threads=1, max_transition=.5, die_size=args.die_size,
                resume_placement=args.resume_placement,
                resume_routed_database=args.resume_routed_database)
            if implement(options) != 0:
                raise ValueError('initialized-lane physical checks failed')
            report['status'] = 'component_checks_pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)
