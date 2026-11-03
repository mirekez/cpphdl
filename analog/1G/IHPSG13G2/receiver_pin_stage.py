#!/usr/bin/env python3
"""Repair equivalent gate inputs on a completed receiver global checkpoint.

The preceding buffer/function proofs remain bound to their original artifacts.
This stage adds an exact pin-permutation proof, fresh routing and both RC
screens. A successful result still requires detailed routing and extraction.
"""
import argparse
import gc
import json
import os
from pathlib import Path
import time

from buffer_equivalence import compare as buffer_compare
from pin_swap_equivalence import compare as pin_compare
from rebuffer_equivalence import compare as rebuffer_compare
from receiver_final_repair import final_screen
from receiver_path_sizing import HERE, ROOT, digest, slack
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


SCRIPT = '''set work $::env(REPEATER_WORK)
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$::env(RECEIVER_SEED)"
read_sdc "$::env(RECEIVER_SDC)"
set_propagated_clock [all_clocks]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
apply_routing_capacitance_margin 1.75 "$libroot/lef/sg13g2_tech.lef"
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
check_placement -verbose
global_route
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/before_setup.rpt"
report_worst_slack -min -digits 9 > "$work/before_hold.rpt"
source "$::env(RECEIVER_SOURCE)/receiver_pin_repair.tcl"
receiver_pin_repair $work
if {[info exists ::env(RECEIVER_REPAIR_HOLD)] && $::env(RECEIVER_REPAIR_HOLD) == "1"} {
    # Setup optimization can shorten fast-corner paths even without removing
    # buffers. Restore hold after pin repair, then prove the inserted identity
    # cells separately from the equivalent input permutations.
    global_route
    estimate_parasitics -placement
    estimate_parasitics -global_routing
    write_verilog "$work/before_hold.v"
    report_worst_slack -min -digits 9 > "$work/hold_seed.rpt"
    for {set pass 0} {$pass < 3 && [sta::worst_slack_cmd min] < 0.1e-9} {incr pass} {
        repair_timing -hold -hold_margin 0.15 -setup_margin 0.1
        detailed_placement
        check_placement -verbose
        global_route
        estimate_parasitics -placement
        estimate_parasitics -global_routing
    }
    write_verilog "$work/after_hold.v"
}
puts "RECEIVER_FINAL_ROUTE_CHECK_BEGIN"
global_route
estimate_parasitics -placement
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/margin_setup.rpt"
report_worst_slack -min -digits 9 > "$work/margin_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/margin_electrical.rpt"
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \\
    -fields {slew cap fanout input_pins} -digits 9 > "$work/margin_paths.rpt"
apply_routing_capacitance_margin 1 "$libroot/lef/sg13g2_tech.lef"
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/global_electrical.rpt"
write_verilog "$work/global.v"
write_guides "$work/global.guide"
write_db "$work/global.odb"
puts "RECEIVER_FINAL_ROUTE_CHECK_END"
exit
'''


def require_origin(proof):
    # A failed timing screen may supply diagnostic geometry, but incomplete
    # native work or missing logical proofs may never supply the next stage.
    if proof.get('status') not in ('fail', 'global_repair_candidate') or not any(
            s.get('name') == 'global' and s.get('returncode') == 0 for s in proof.get('stages', [])):
        raise ValueError('completed global repair required for pin repair')
    for name in ('seed_connectivity', 'filler_connectivity', 'buffer_connectivity', 'placement_connectivity'):
        if proof.get(name, {}).get('status') != 'pass':
            raise ValueError('missing preceding connectivity proof: ' + name)
    if proof.get('global_electrical_pass') is not True:
        raise ValueError('repair electrical violations before pin-only repair')


def run(args):
    repair_hold = getattr(args, 'repair_hold', False)
    origin, work = args.origin.resolve(), args.output.resolve()
    proof = json.loads((origin / 'checks.json').read_text())
    require_origin(proof)
    hashes = dict(proof['source_sha256'])
    for name, value in proof['artifact_sha256'].items():
        if name in hashes and hashes[name] != value:
            raise ValueError('conflicting origin provenance')
        hashes[name] = value
    constraints, models = args.constraints.resolve(), args.models.resolve()
    tools, pdk = args.tools.resolve(), args.pdk.resolve()
    required = [origin / 'global.odb', origin / 'global.v', constraints, models,
                tools / 'bin/openroad', tools / 'root/usr/bin/openroad']
    required += [pdk / ('ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_' + c + '.lib')
                 for c in ('slow_1p08V_125C', 'fast_1p32V_m40C')]
    for p in required:
        if hashes.get(str(p)) != digest(p):
            raise ValueError('pin repair input differs from producer: ' + str(p))
    for p in (Path(__file__).resolve(), HERE / 'receiver_pin_repair.tcl', HERE / 'pin_swap_equivalence.py',
              HERE / 'buffer_equivalence.py', HERE / 'rebuffer_equivalence.py',
              HERE / 'sizing_equivalence.py', origin / 'checks.json'):
        value = digest(p)
        if str(p) in hashes and hashes[str(p)] != value:
            raise ValueError('preceding proof source changed: ' + str(p))
        hashes[str(p)] = value

    def verify():
        for name, value in hashes.items():
            if digest(name) != value:
                raise ValueError('pin repair provenance changed: ' + name)

    verify()
    work.mkdir(parents=True, exist_ok=False)
    script = work / 'repair.tcl'
    script.write_text(SCRIPT)
    hashes[str(script)] = digest(script)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(), stages=[],
                  source_sha256=hashes, preceding_repair=str(origin), repair_hold=repair_hold, timing_qualified=False,
                  physical_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['receiver-pin-repair'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, REPEATER_WORK=str(work), REPEATER_PDK=str(pdk),
                RECEIVER_SEED=str(origin / 'global.odb'), RECEIVER_SDC=str(constraints),
                RECEIVER_SOURCE=str(HERE), REPEATER_SOURCE=str(ROOT / 'pdk/1G_repeater_IHPSG13G2'),
                RECEIVER_REPAIR_HOLD='1' if repair_hold else '0',
                OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            report['stage'] = 'global'
            save()
            started = time.monotonic()
            with (work / 'global.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                    tools / 'bin/openroad', '-no_init', '-exit', script], env=environment,
                    stdout=log, stderr=-2, timeout=3600)
            report['stages'].append(dict(name='global', returncode=result.returncode,
                                         seconds=time.monotonic() - started))
            if result.returncode:
                raise ValueError('native pin repair failed; see global.log')
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                if abs(slack(work / ('before_' + kind + '.rpt'), mode) -
                       slack(origin / ('margin_' + kind + '.rpt'), mode)) > .001:
                    raise ValueError('reloaded enlarged-C timing differs by more than 1 ps')
            report['seed_connectivity'] = buffer_compare(origin / 'global.v', work / 'before_pins.v')
            if report['seed_connectivity']['removed_buffers']:
                raise ValueError('pin stage changed the input checkpoint')
            # These proofs describe the preceding repair; its full receipt and
            # every artifact remain in source_sha256. Add the new stage explicitly.
            for name in ('filler_connectivity', 'buffer_connectivity'):
                report[name] = proof[name]
            report['pin_connectivity'] = pin_compare(work / 'before_pins.v', work / 'after_pins.v', models)
            gc.collect()
            placed = work / 'after_pins.v'
            if repair_hold:
                report['hold_seed_connectivity'] = buffer_compare(placed, work / 'before_hold.v')
                if report['hold_seed_connectivity']['removed_buffers']:
                    raise ValueError('hold reload changed pin-repaired topology')
                report['hold_connectivity'] = rebuffer_compare(work / 'before_hold.v', work / 'after_hold.v', models)
                placed = work / 'after_hold.v'
            report['placement_connectivity'] = buffer_compare(placed, work / 'global.v')
            if report['placement_connectivity']['removed_buffers']:
                raise ValueError('pin stage unexpectedly removed buffers after proof')
            report['global_electrical_pass'] = 'VIOLATED' not in (work / 'global_electrical.rpt').read_text().upper()
            report['global_estimates'] = dict(setup_ns=slack(work / 'global_setup.rpt', 'max'),
                                             hold_ns=slack(work / 'global_hold.rpt', 'min'))
            verify()
            report['margin_screen'] = final_screen(work)
            report.update(status='global_repair_candidate', stage='complete')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir()
            if p.is_file() and p.name not in ('checks.json', 'checks.tmp')}
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('origin', 'constraints', 'models', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--repair-hold', action='store_true',
                        help='repair fast-corner hold after equivalent pin swaps, with a separate endpoint proof')
    run(parser.parse_args())
