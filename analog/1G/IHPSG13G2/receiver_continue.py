#!/usr/bin/env python3
"""Continue a checked receiver repair through routing, replay and layout.

Wait for live repair and disk availability without holding the EDA memory slot.
This produces standalone receiver evidence, not complete-chip qualification.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import time

from receiver_final_repair import final_screen
from receiver_layout import wait_for_producer
from receiver_gate_replay import HERE, ROOT, digest, verify_files
from resume_receiver_route import diagnostic_inputs
from tool_process import configure_cleanup, execute


def require_receipt(directory, status):
    proof = json.loads((directory / 'checks.json').read_text())
    if proof.get('status') != status or proof.get('stage') != 'complete':
        raise ValueError('receiver stage did not pass: ' + str(directory))
    return proof


def wait_for_space(directory, minimum=3 * 1024**3, timeout=86400):
    deadline = time.monotonic() + timeout
    while shutil.disk_usage(directory).free < minimum:
        if time.monotonic() >= deadline:
            raise TimeoutError('receiver continuation needs at least 3 GiB free')
        time.sleep(min(15, max(0, deadline - time.monotonic())))


def run(args):
    repair, mapped, work = (p.resolve() for p in (args.repair, args.mapped_work, args.output))
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_repair', pid=os.getpid(), stages=[],
                  full_chip_qualified=False, scope=__doc__)
    hashes = {str(p): digest(p) for p in (Path(__file__).resolve(), HERE / 'receiver_final_repair.py',
        HERE / 'receiver_layout.py', HERE / 'receiver_gate_replay.py', HERE / 'resume_receiver_route.py',
        HERE / 'receiver_route_probe.py')}
    report['source_sha256'] = hashes

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        probe = getattr(args, 'route_probe', None)
        route_options = []
        if probe:
            probe = probe.resolve()
            report['stage'] = 'waiting_for_route_probe'
            save()
            wait_for_producer(probe, 'early_route_diagnostic_complete', 18000)
            inputs, _, _ = diagnostic_inputs(repair, args.constraints.resolve(), probe)
            proof = dict(source_sha256=inputs, artifact_sha256=inputs)
            route_options = ['--route-probe', probe]
        else:
            wait_for_producer(repair, 'global_repair_candidate', 7200)
            proof = require_receipt(repair, 'global_repair_candidate')
            verify_files(proof['source_sha256'])
            verify_files(proof['artifact_sha256'])
            if final_screen(repair) != proof.get('margin_screen'):
                raise ValueError('receiver repair screen differs from its receipt')
        if getattr(args, 'allocator', None):
            allocator = args.allocator.resolve()
            hashes[str(allocator)] = digest(allocator)
            route_options += ['--allocator', allocator]
        report['repair_receipt_sha256'] = digest(repair / 'checks.json')
        report['stage'] = 'waiting_for_disk_space'
        report['minimum_free_bytes'] = 3 * 1024**3
        save()
        wait_for_space(work)
        verify_files(hashes)
        verify_files(proof['source_sha256'])
        verify_files(proof['artifact_sha256'])
        if digest(repair / 'checks.json') != report['repair_receipt_sha256']:
            raise ValueError('receiver repair receipt changed while waiting')
        python = args.pdk.resolve() / '.cpphdl/venv/bin/python'
        route, replay, layout = (work / name for name in ('route', 'replay', 'layout'))
        stages = [
            ('route', 'resume_receiver_route.py', [
                '--origin', repair, '--constraints', args.constraints.resolve(), '--output', route,
                '--pdk', args.pdk.resolve(), '--tools', args.tools.resolve(), *route_options],
                route, 'routed_timing_candidate_pass', 28800),
            ('replay', 'receiver_gate_replay.py', [
                '--route', route, '--mapped-work', mapped, '--output', replay],
                replay, 'receiver_replay_pass', 11400),
            ('layout', 'receiver_layout.py', [
                '--route', route, '--replay', replay, '--mapped-work', mapped, '--output', layout,
                '--pdk', args.pdk.resolve(), '--tools', args.tools.resolve()],
                layout, 'receiver_geometry_pass', 43200),
        ]
        for name, script, options, directory, expected, timeout in stages:
            verify_files(hashes)
            report['stage'] = name
            save()
            command = [python, HERE / script, *options]
            started = time.monotonic()
            with (work / (name + '.log')).open('w') as log:
                result = execute(command, cwd=ROOT, stdout=log, stderr=-2,
                                 timeout=timeout, memory_exclusive=False)
            report['stages'].append(dict(name=name, returncode=result.returncode,
                seconds=time.monotonic() - started, command=list(map(str, command))))
            if result.returncode:
                raise ValueError('receiver continuation failed: ' + name)
            # A diagnostic producer can exit zero while reporting failed STA.
            # Never advance from process exit status alone.
            require_receipt(directory, expected)
            report[name + '_receipt_sha256'] = digest(directory / 'checks.json')
            save()
        verify_files(hashes)
        report.update(status='receiver_standalone_checks_pass', stage='complete')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('repair', 'mapped-work', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--route-probe', type=Path,
                        help='wait for an extracted early route of the diagnostic repair')
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--allocator', type=Path, help='explicit local libjemalloc.so.2 for routing')
    run(parser.parse_args())
