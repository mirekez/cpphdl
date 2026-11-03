#!/usr/bin/env python3
"""Stream and check the exact receiver that passed routed timing and gate replay.

This qualifies standalone geometry/connectivity only. Post-fill coupling,
complete-chip integration and electrical signoff remain separate requirements.
"""
import argparse
import gc
import json
import os
from pathlib import Path
import shutil
import sys
import time

from receiver_gate_replay import CASES, HERE, ROOT, digest, exact_trace, inputs, verify_files
from buffer_equivalence import parse

COMMON = ROOT / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(COMMON))
from checks import check_drc, check_lvs, check_timing
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def add_hashes(target, values):
    for name, value in values.items():
        if name in target and target[name] != value:
            raise ValueError('conflicting layout provenance: ' + name)
        target[name] = value


def accepted_inputs(route, replay, mapped):
    """Revalidate the native oracle, routed evidence and every replay trace."""
    hashes = inputs(route, mapped)
    proof = json.loads((replay / 'checks.json').read_text())
    if (proof.get('status') != 'receiver_replay_pass' or proof.get('stage') != 'complete'
            or proof.get('functional_qualified') is not True):
        raise ValueError('completed exact receiver replay required before layout')
    replay_inputs = proof.get('input_sha256', {})
    verify_files(replay_inputs)
    if any(replay_inputs.get(name) != value for name, value in hashes.items()):
        raise ValueError('replay does not bind this routed receiver and native oracle')
    artifacts = proof.get('artifact_sha256', {})
    verify_files(artifacts)
    for mode, _, reference in CASES:
        trace = replay / (mode + '_trace.csv')
        value = exact_trace(trace, mapped / reference)
        if artifacts.get(str(trace)) != value:
            raise ValueError('unbound receiver replay trace: ' + mode)
    executable = replay / 'obj/VPCSReceiver'
    if proof.get('executable_sha256') != digest(executable):
        raise ValueError('replayed executable changed')
    for group in (replay_inputs, artifacts):
        add_hashes(hashes, group)
    for path in (replay / 'checks.json', executable):
        add_hashes(hashes, {str(path): digest(path)})
    # DEF and CDL are emitted from the same extracted ODB as the replay netlist.
    # Require the producer to bind them, rather than accepting arbitrary copies.
    route_proof = json.loads((route / 'checks.json').read_text())
    for name in ('core.def', 'core.cdl', 'route_drc.rpt'):
        path = route / name
        if route_proof['artifact_sha256'].get(str(path)) != digest(path):
            raise ValueError('unbound receiver layout artifact: ' + name)
    if (route / 'route_drc.rpt').read_text().strip():
        raise ValueError('receiver detailed-router violations remain')
    return hashes


def wait_for_producer(directory, expected, timeout):
    """Wait outside the EDA slot so the producer can acquire it and finish."""
    deadline = time.monotonic() + timeout
    while True:
        report = json.loads((directory / 'checks.json').read_text())
        if report.get('status') == expected and report.get('stage') == 'complete':
            return
        if report.get('status') != 'running':
            raise ValueError('upstream receiver run failed: ' + str(directory))
        pid = report.get('pid')
        if isinstance(pid, bool) or not isinstance(pid, int) or pid <= 1:
            raise ValueError('upstream receiver run has no live producer')
        try:
            os.kill(pid, 0)
        except OSError as error:
            raise ValueError('upstream receiver producer stopped') from error
        if time.monotonic() >= deadline:
            raise TimeoutError('receiver dependency did not finish: ' + str(directory))
        time.sleep(min(15, max(0, deadline - time.monotonic())))


def physical_inputs(pdk, tools):
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    files = {Path(__file__).resolve(), HERE / 'buffer_equivalence.py',
             COMMON / 'checks.py', COMMON / 'tool_process.py', COMMON / 'stream.py',
             COMMON / 'route_tech.py', COMMON / 'fill_core.py', COMMON / 'fill_remainder.py',
             tools / 'bin/klayout', tools / 'root/usr/bin/klayout',
             pdk / '.cpphdl/venv/bin/python', pdk / 'versions.txt',
             tech / 'sg13g2.lyt', tech / 'sg13g2.map',
             tech / 'drc/run_drc.py', tech / 'lvs/run_lvs.py',
             library / 'lef/sg13g2_tech.lef', library / 'lef/sg13g2_stdcell.lef',
             library / 'gds/sg13g2_stdcell.gds', library / 'cdl/sg13g2_stdcell.cdl'}
    for group in ('drc', 'lvs'):
        files.update(p for p in (tech / group).rglob('*') if p.is_file()
                     and p.suffix in ('.drc', '.lvs', '.json', '.py', '.rb'))
    for group in ('ActGatP', 'Metal', 'TopMetal'):
        files.add(tech / f'macros/sg13g2_filler_{group}.lym')
    return {str(path): digest(path) for path in sorted(files)}


def expected_ports(netlist):
    design = parse(netlist)
    if design['module'] != 'PCSReceiver':
        raise ValueError('receiver layout requires PCSReceiver')
    return {'VDD', 'VSS'} | {name if bit is None else f'{name}[{bit}]'
                            for name, bit in design['ports']}


def run(args):
    route, replay, mapped, work = (path.resolve() for path in
                                  (args.route, args.replay, args.mapped_work, args.output))
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_dependencies', pid=os.getpid(), stages=[],
                  top='PCSReceiver', scope=__doc__, geometry_qualified=False,
                  postfill_timing_qualified=False, full_chip_qualified=False)
    hashes = {}

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        if args.wait:
            wait_for_producer(route, 'routed_timing_candidate_pass', 28800)
            wait_for_producer(replay, 'receiver_replay_pass', 14400)
        report['stage'] = 'waiting_for_memory_slot'
        save()
        with memory_slot(['receiver-layout'], exclusive=True) as owned:
            hashes = accepted_inputs(route, replay, mapped)
            add_hashes(hashes, physical_inputs(pdk, tools))
            report['input_sha256'] = hashes
            report['prefill_timing'] = check_timing(route)
            ports = expected_ports(route / 'postroute.v')
            report['expected_ports'] = sorted(ports)
            if shutil.disk_usage(work).free < 1024**3:
                raise ValueError('receiver layout requires at least 1 GiB free')
            for name in ('core.def', 'core.cdl'):
                shutil.copyfile(route / name, work / name)
                add_hashes(hashes, {str(work / name): digest(work / name)})
                if hashes[str(work / name)] != hashes[str(route / name)]:
                    raise ValueError('routed artifact changed while copying: ' + name)
            environment = dict(os.environ, REPEATER_PDK=str(pdk), REPEATER_WORK=str(work),
                REPEATER_SOURCE=str(COMMON), REPEATER_TOP='PCSReceiver', OMP_NUM_THREADS='1',
                MALLOC_ARENA_MAX='2', PATH=str(tools / 'bin') + os.pathsep + os.environ.get('PATH', ''))
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            python = pdk / '.cpphdl/venv/bin/python'
            tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
            library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'

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
                    raise ValueError('receiver layout stage failed: ' + name)
                verify_files(hashes)
                save()

            gc.collect()
            stage('technology', [python, COMMON / 'route_tech.py', '--pdk', pdk, '--work', work], 60)
            add_hashes(hashes, {str(work / 'route_tech.lef'): digest(work / 'route_tech.lef')})
            stage('stream', [python, COMMON / 'stream.py', '--pdk', pdk, '--work', work,
                             '--top', 'PCSReceiver'], 1800)
            stage('fill', [tools / 'bin/klayout', '-zz', '-r', COMMON / 'fill_core.py', '-rd',
                'output_file=' + str(work / 'module_filled.gds'), work / 'repeater_core_unfilled.gds'], 7200)
            filled = work / 'module_filled.gds'
            add_hashes(hashes, {str(filled): digest(filled)})
            # Both checkers must consume this exact filled GDS. DRC completion,
            # all rule groups and persisted transistor/port LVS are mandatory.
            stage('drc', [python, tech / 'drc/run_drc.py', '--path', filled, '--topcell', 'PCSReceiver',
                '--run_dir', work / 'drc', '--mp', '1', '--density_thr', '1', '--antenna'], 14400)
            report['drc'] = check_drc(work / 'drc/module_filled_PCSReceiver_full.lyrdb', tech / 'drc')
            (work / 'lvs.cir').write_text(f'.include "{library / "cdl/sg13g2_stdcell.cdl"}"\n'
                                         f'.include "{work / "core.cdl"}"\n')
            add_hashes(hashes, {str(work / 'lvs.cir'): digest(work / 'lvs.cir')})
            stage('lvs', [python, tech / 'lvs/run_lvs.py', '--layout', filled,
                '--netlist', work / 'lvs.cir', '--topcell', 'PCSReceiver', '--run_mode', 'deep',
                '--run_dir', work / 'lvs'], 14400)
            report['lvs'] = check_lvs(work / 'lvs/module_filled.lvsdb', 'PCSReceiver', ports)
            verify_files(hashes)
            report.update(status='receiver_geometry_pass', stage='complete', geometry_qualified=True,
                          gds=str(filled))
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.rglob('*')
                                    if p.is_file() and p.name not in ('checks.json', 'checks.tmp')}
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('route', 'replay', 'mapped-work', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--wait', action='store_true', help='wait for live route/replay producers before taking the EDA slot')
    run(parser.parse_args())
