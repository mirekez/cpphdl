#!/usr/bin/env python3
"""Validate a saved local-ECO global checkpoint before continuing physical routing.

Adopt geometry only. Recheck signal endpoints, global electrical constraints,
detailed routing and fresh extracted timing. Full replay/DRC/LVS remain separate.
"""
import argparse
import gc
import json
import os
from pathlib import Path
import shutil
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
COMMON = ROOT / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(COMMON))
from buffer_equivalence import compare
from checks import check_timing
from receiver_path_sizing import digest, slack
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot
from receiver_route_probe import screen as diagnostic_screen
import receiver_allocator


def diagnostic_inputs(origin, constraints, probe):
    """Bind a completed early-route measurement to the exact global candidate.

    Early routing is only an admission screen. The caller must still run full
    routing, extract again, and prove the final signal endpoints and timing.
    """
    proof = json.loads((origin / 'checks.json').read_text())
    diagnostic_screen(proof)
    if constraints != origin / 'candidate.sdc':
        raise ValueError('diagnostic continuation requires its saved constraints')
    if probe is None:
        raise ValueError('diagnostic continuation requires an extracted route probe')
    measured = json.loads((probe / 'checks.json').read_text())
    if (measured.get('status') != 'early_route_diagnostic_complete' or
            measured.get('stage') != 'complete' or measured.get('detail_end_iteration') != 1 or
            measured.get('connectivity', {}).get('status') != 'pass' or
            measured.get('connectivity', {}).get('top') != 'PCSReceiver'):
        raise ValueError('completed early-route receiver endpoint proof required')
    hashes = {}
    for group in (proof['source_sha256'], measured['source_sha256'], measured['artifact_sha256']):
        for name, value in group.items():
            if name in hashes and hashes[name] != value:
                raise ValueError('conflicting diagnostic provenance: ' + name)
            if digest(name) != value:
                raise ValueError('changed diagnostic input: ' + name)
            hashes[name] = value
    for name in ('candidate.odb', 'candidate.sdc', 'after.v'):
        path = origin / name
        value = digest(path)
        if (proof['artifact_sha256'].get(str(path)) != value or
                measured['source_sha256'].get(str(path)) != value):
            raise ValueError('probe did not measure this diagnostic candidate: ' + name)
    receipt = origin / 'checks.json'
    if measured['source_sha256'].get(str(receipt)) != digest(receipt):
        raise ValueError('probe did not bind this diagnostic receipt')
    cells = Path(proof['cell_models'])
    if hashes.get(str(cells)) != digest(cells):
        raise ValueError('missing diagnostic cell models')
    for name in ('setup.rpt', 'hold.rpt', 'electrical.rpt', 'core.spef', 'postroute.v'):
        path = probe / name
        if measured['artifact_sha256'].get(str(path)) != digest(path):
            raise ValueError('unbound early-route measurement: ' + name)
    timing = check_timing(probe)
    if (measured.get('setup_ns') != timing['setup_slack_ns'] or
            measured.get('hold_ns') != timing['hold_slack_ns'] or
            measured.get('electrical_pass') is not True):
        raise ValueError('early-route timing differs from its receipt')
    endpoints = compare(probe / 'postroute.v', origin / 'after.v', defer_fillers=True)
    if endpoints['removed_buffers']:
        raise ValueError('early route changed diagnostic signal cells')
    hashes[str(probe / 'checks.json')] = digest(probe / 'checks.json')
    return hashes, origin / 'candidate.odb', origin / 'after.v'


VALIDATE = '''set work $::env(REPEATER_WORK)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$work/global.odb"
if {[[ord::get_db_block] getName] != "PCSReceiver"} { error "receiver-only checkpoint" }
read_sdc "$::env(RECEIVER_SDC)"
set_propagated_clock [all_clocks]
set_max_transition 0.5 [current_design]
set_max_fanout 8 [current_design]
set_max_capacitance 0.1 [current_design]
check_placement -verbose
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
global_route
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/global_electrical.rpt"
write_verilog "$work/global.v"
exit
'''


def run(args):
    origin, constraints, work = (p.resolve() for p in (args.origin, args.constraints, args.output))
    proof = json.loads((origin / 'checks.json').read_text())
    probe = getattr(args, 'route_probe', None)
    if proof.get('status') == 'diagnostic_complete':
        hashes, database, netlist = diagnostic_inputs(origin, constraints, probe.resolve() if probe else None)
    else:
        if probe:
            raise ValueError('route probe is only valid with a completed diagnostic')
        if not any(s['name'] == 'global' and s['returncode'] == 0 for s in proof.get('stages', [])):
            raise ValueError('completed global checkpoint required')
        for name in ('seed_connectivity', 'buffer_connectivity', 'filler_connectivity', 'placement_connectivity'):
            if proof.get(name, {}).get('status') != 'pass':
                raise ValueError('missing complete global ECO endpoint proof: ' + name)
        hashes = dict(proof['source_sha256'])
        database, netlist = origin / 'global.odb', origin / 'global.v'
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    allocator_env, allocator, allocator_files = receiver_allocator.configuration(getattr(args, 'allocator', None))
    for p in (constraints, tools / 'bin/openroad', tools / 'root/usr/bin/openroad',
              tools / 'rcx_patterns.rules', HERE / 'receiver_local_route.tcl'):
        if hashes.get(str(p)) != digest(p):
            raise ValueError('route resume input differs from the producer: ' + str(p))
    for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        p = pdk / ('ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_' + corner + '.lib')
        if hashes.get(str(p)) != digest(p):
            raise ValueError('route resume requires the producer timing libraries')
    for p in (Path(__file__).resolve(), origin / 'checks.json', database, netlist,
              HERE / 'receiver_route_probe.py', HERE / 'buffer_equivalence.py',
              HERE / 'receiver_path_sizing.py', COMMON / 'checks.py', COMMON / 'tool_process.py'):
        if str(p) in hashes and hashes[str(p)] != digest(p):
            raise ValueError('route resume provenance changed: ' + str(p))
        hashes[str(p)] = digest(p)
    for p in [HERE / 'receiver_allocator.py', Path('/usr/bin/time')] + allocator_files:
        if str(p) in hashes and hashes[str(p)] != digest(p):
            raise ValueError('route allocator provenance changed: ' + str(p))
        hashes[str(p)] = digest(p)

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('route resume input changed: ' + name)

    verify()
    if shutil.disk_usage(work.parent).free < 1000 * 1024**2:
        raise ValueError('route resume requires at least 1000 MiB free')
    work.mkdir(parents=True, exist_ok=False)
    # The checkpoint is immutable; each native stage writes a distinct file.
    # Linking avoids duplicating a large retained database on the same disk.
    os.link(database, work / 'global.odb')
    validation = work / 'validate.tcl'
    validation.write_text(VALIDATE)
    hashes[str(validation)] = digest(validation)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(), stages=[],
        source_sha256=hashes, allocator=allocator, adopted_checkpoint_sha256=digest(work / 'global.odb'),
        checkpoint_scope='geometry adoption with new endpoint/electrical checks; no inherited pass',
        timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['receiver-route-resume'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, REPEATER_WORK=str(work), REPEATER_PDK=str(pdk),
                REPEATER_TOOLS=str(tools), REPEATER_SOURCE=str(COMMON), RECEIVER_SDC=str(constraints),
                RECEIVER_ROUTE_PROBE='0', OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            environment = receiver_allocator.environment(environment, allocator_env)
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())

            def stage(name, script, timeout, gib):
                report['stage'] = name
                save()
                environment['RECEIVER_ECO_STAGE'] = name
                start = time.monotonic()
                with (work / (name + '.log')).open('w') as log:
                    result = execute(['/usr/bin/time', '-v', '-o', work / (name + '.resources'),
                        'prlimit', '--as=' + str(gib * 1024**3), '--core=0', '--',
                        tools / 'bin/openroad', '-no_init', '-exit', script], env=environment,
                        stdout=log, stderr=-2, timeout=timeout)
                report['stages'].append(dict(name=name, returncode=result.returncode,
                                            seconds=time.monotonic() - start))
                save()
                if result.returncode:
                    raise ValueError(name + ' failed; see ' + name + '.log')
                verify()

            stage('validate', validation, 600, 4)
            report['checkpoint_connectivity'] = compare(netlist, work / 'global.v')
            if report['checkpoint_connectivity']['removed_buffers']:
                raise ValueError('saved ODB differs from the checkpoint netlist')
            report['global_estimates'] = dict(setup_ns=slack(work / 'global_setup.rpt', 'max'),
                                            hold_ns=slack(work / 'global_hold.rpt', 'min'))
            if any(v < 0 for v in report['global_estimates'].values()):
                raise ValueError('global setup/hold must pass before detailed routing')
            if 'VIOLATED' in (work / 'global_electrical.rpt').read_text().upper():
                raise ValueError('global electrical limits fail before detailed routing')
            if proof.get('status') == 'diagnostic_complete':
                differences = {key: value - proof['rounds'][-1][key]
                               for key, value in report['global_estimates'].items()}
                report['global_reload_difference_ns'] = differences
                if any(abs(value) > .001 for value in differences.values()):
                    raise ValueError('global checkpoint timing changed by more than 1 ps')
            save()
            gc.collect()
            # The revised placement takes longer per routing iteration than
            # the old decoder. Keep a finite limit without killing closure
            # merely because an earlier implementation routed in four hours.
            stage('detail', HERE / 'receiver_local_route.tcl', 21600, 6)
            if (work / 'route_drc.rpt').read_text().strip():
                raise ValueError('detailed router reports violations')
            stage('extract', HERE / 'receiver_local_route.tcl', 1800, 4)
            report['routed_connectivity'] = compare(work / 'postroute.v', work / 'global.v', defer_fillers=True)
            report.update(setup_ns=slack(work / 'setup.rpt', 'max'), hold_ns=slack(work / 'hold.rpt', 'min'))
            try:
                report['timing'] = check_timing(work)
                report['status'] = 'routed_timing_candidate_pass'
            except ValueError as error:
                report.update(status='routed_timing_fail', timing_error=str(error))
            verify()
            report.update(stage='complete', artifact_sha256={str(p): digest(p)
                for p in work.iterdir() if p.is_file() and p.name != 'checks.json'})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('origin', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--route-probe', type=Path,
                        help='completed extracted admission screen for a diagnostic origin')
    parser.add_argument('--allocator', type=Path, help='explicit local libjemalloc.so.2 for routing')
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
