#!/usr/bin/env python3
"""Resume exact receiver gate replay independently of expensive physical routing.

This produces functional evidence only. Layout, extracted timing and complete
chip qualification remain separate. No clocks or vectors are shortened.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from checks import check_timing
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot

CASES = (('normal', 'receiver-vectors.txt', 'cpp_trace.csv'),
         ('faults', 'receiver-fault-vectors.txt', 'cpp_fault_trace.csv'),
         ('enable', 'receiver-vectors.txt', 'cpp_enable_trace.csv'))


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_files(hashes):
    if not hashes:
        raise ValueError('missing replay provenance')
    for name, expected in hashes.items():
        if digest(Path(name)) != expected:
            raise ValueError('changed replay input: ' + name)


def inputs(route, mapped):
    """Require finished route/timing and the independent native oracle."""
    receipt = route / 'checks.json'
    proof = json.loads(receipt.read_text())
    if (proof.get('status') != 'routed_timing_candidate_pass'
            or proof.get('stage') != 'complete'
            or proof.get('routed_connectivity', {}).get('status') != 'pass'
            or proof.get('routed_connectivity', {}).get('top') != 'PCSReceiver'):
        raise ValueError('receiver route, timing and endpoint proof must finish successfully')
    sources, artifacts = proof.get('source_sha256', {}), proof.get('artifact_sha256', {})
    verify_files(sources)
    verify_files(artifacts)
    for name in ('postroute.v', 'core.odb', 'core.spef', 'setup.rpt', 'hold.rpt', 'electrical.rpt'):
        if artifacts.get(str(route / name)) != digest(route / name):
            raise ValueError('unbound routed artifact: ' + name)
    check_timing(route)
    hardware = mapped / 'hardware_checks.json'
    oracle = json.loads(hardware.read_text())
    if (oracle.get('status') != 'pass' or oracle.get('module') != 'PCSReceiver'
            or not oracle.get('native_mapped_exact') or not oracle.get('native_rtl_exact')
            or oracle.get('mapped_sha256') != digest(mapped / 'mapped.v')):
        raise ValueError('independent receiver oracle is missing or stale')
    verify_files(oracle.get('source_sha256', {}))
    for name in ('receiver.cpp', 'receiver_fault_checks.hpp', 'receiver_enable_checks.hpp'):
        path = HERE.parent / 'tests' / name
        if oracle['source_sha256'].get(str(path)) != digest(path):
            raise ValueError('receiver harness is not bound to the native oracle: ' + name)
    hashes = dict(sources)
    for group in (artifacts, oracle['source_sha256']):
        for name, value in group.items():
            if name in hashes and hashes[name] != value:
                raise ValueError('conflicting receiver provenance: ' + name)
            hashes[name] = value
    # The retained fixture/trace copies must be the oracle's hashed artifacts.
    for name in {name for _, fixture, trace in CASES for name in (fixture, trace)}:
        expected = {value for path, value in oracle['source_sha256'].items() if Path(path).name == name}
        if expected != {digest(mapped / name)}:
            raise ValueError('receiver fixture or oracle trace differs: ' + name)
        hashes[str(mapped / name)] = digest(mapped / name)
    models = digest(mapped / 'cells.v')
    if models not in {value for name, value in sources.items() if Path(name).name == 'cells.v'}:
        raise ValueError('receiver gate models differ from routed source models')
    for path in (receipt, hardware, mapped / 'mapped.v', mapped / 'cells.v', Path(__file__).resolve(),
                 ROOT / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
                 ROOT / 'pdk/1G_repeater_IHPSG13G2/checks.py'):
        hashes[str(path)] = digest(path)
    return hashes


def exact_trace(actual, reference):
    if not actual.is_file() or not actual.stat().st_size or digest(actual) != digest(reference):
        raise ValueError('receiver trajectory differs: ' + actual.name)
    return digest(actual)


def run(args):
    route, mapped, work = (p.resolve() for p in (args.route, args.mapped_work, args.output))
    work.mkdir(parents=True, exist_ok=args.resume)
    report_path = work / 'checks.json'
    previous = json.loads(report_path.read_text()) if args.resume else None
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(), stages=[],
                  scope=__doc__, functional_qualified=False, physical_qualified=False,
                  full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(report_path)

    save()
    try:
        with memory_slot(['receiver-gate-replay'], exclusive=True) as owned:
            # The producer releases the slot immediately before its final
            # atomic receipt write. Allow that handoff, never infer a pass.
            for _ in range(240):
                pending = json.loads((route / 'checks.json').read_text())
                if pending.get('status') != 'running':
                    break
                producer = pending.get('pid')
                if not isinstance(producer, int) or producer <= 1:
                    break
                try:
                    os.kill(producer, 0)
                except OSError:
                    break
                time.sleep(.25)
            hashes = inputs(route, mapped)
            verilator, cxx = Path(args.verilator).resolve(), Path(args.cxx).resolve()
            binary = verilator.with_name('verilator_bin')
            for path in (verilator, binary, cxx):
                hashes[str(path)] = digest(path)
            if previous is not None and previous.get('input_sha256') != hashes:
                raise ValueError('cannot resume a replay with changed inputs')
            report['input_sha256'] = hashes
            environment = dict(os.environ, OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())

            def stage(name, command, timeout):
                report['stage'] = name
                save()
                started = time.monotonic()
                with (work / (name + '.log')).open('w') as log:
                    result = execute(command, cwd=work, env=environment, stdout=log, stderr=-2,
                                     timeout=timeout)
                report['stages'].append(dict(name=name, returncode=result.returncode,
                    seconds=time.monotonic() - started, command=list(map(str, command))))
                if result.returncode:
                    raise ValueError('receiver replay stage failed: ' + name)
                verify_files(hashes)
                save()

            executable = work / 'obj/VPCSReceiver'
            if previous and previous.get('executable_sha256'):
                if digest(executable) != previous['executable_sha256']:
                    raise ValueError('receiver replay executable changed')
                report['executable_sha256'] = previous['executable_sha256']
                report['resumed_build'] = True
            else:
                if shutil.disk_usage(work).free < 512 * 1024**2:
                    raise ValueError('receiver replay build requires at least 512 MiB free')
                stage('build', [str(verilator), '--cc', '--exe', '--build', '--no-skip-identical',
                    '--flatten', '--output-groups', '0', '--output-split', '10000',
                    '--output-split-cfuncs', '500', '-j', '1', '--top-module', 'PCSReceiver',
                    '-Wno-fatal', '--Mdir', str(work / 'obj'), '-CFLAGS', '-DVERILATOR -DMAPPED -std=c++17',
                    str(route / 'postroute.v'), str(mapped / 'cells.v'), str(HERE.parent / 'tests/receiver.cpp'),
                    '-MAKEFLAGS', 'CXX=' + str(cxx) + ' LINK=' + str(cxx) + ' OPT_FAST=-O0 OPT_SLOW=-O0'], 7200)
                report['executable_sha256'] = digest(executable)
                save()
            traces = {}
            for mode, fixture, reference in CASES:
                target = work / (mode + '_trace.csv')
                command = [str(executable)] + ([] if mode == 'normal' else ['--' + mode])
                command += [str(mapped / fixture), str(target)]
                stage(mode, command, 900)
                traces[str(target)] = exact_trace(target, mapped / reference)
            if digest(executable) != report['executable_sha256']:
                raise ValueError('receiver replay executable changed during replay')
            verify_files(hashes)
            report.update(status='receiver_replay_pass', stage='complete',
                          functional_qualified=True, artifact_sha256=traces)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('route', 'mapped-work', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--resume', action='store_true', help='reuse only a verified completed executable')
    parser.add_argument('--verilator', default=shutil.which('verilator'))
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
