"""Exercise post-sizing electrical repair on a freshly routed register loop."""
import argparse
import os
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from buffer_equivalence import BUFFERS, connectivity, parse
from receiver_final_repair import final_screen, require_complete_routes
from rebuffer_equivalence import compare as rebuffer_compare
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    with memory_slot(['receiver-final-repair-native'], exclusive=True) as owned, \
            tempfile.TemporaryDirectory(prefix='receiver-final-repair-') as temporary:
        work = Path(temporary)
        fixture, output = work / 'fixture', work / 'output'
        fixture.mkdir()
        output.mkdir()
        (fixture / 'test.v').write_text('''module PCSReceiver(input clk, input reset, output y);
wire q, n1, n2;
sg13g2_dfrbpq_1 state (.CLK(clk), .RESET_B(reset), .D(y), .Q(q));
sg13g2_buf_1 first (.A(q), .X(n1));
sg13g2_buf_1 second (.A(n1), .X(n2));
sg13g2_buf_1 third (.A(n2), .X(y));
endmodule
''')
        if args.isolate:
            # A single-strength gate drives the loaded output directly. Its
            # function must be preserved while a physical buffer takes the load.
            (fixture / 'test.v').write_text('''module PCSReceiver(input clk, input reset, output y);
wire q;
sg13g2_dfrbpq_1 state (.CLK(clk), .RESET_B(reset), .D(y), .Q(q));
sg13g2_a221oi_1 weak (.A1(q), .A2(q), .B1(q), .B2(q), .C1(q), .Y(y));
endmodule
''')
        if args.rebuffer:
            # An intentionally redundant chain makes setup repair exercise
            # buffer removal, rather than passing after drive resizing alone.
            nets = ['q'] + [f'chain{i}' for i in range(11)] + ['y']
            (fixture / 'test.v').write_text(
                'module PCSReceiver(input clk, input reset, output y);\nwire ' + ', '.join(nets[:-1]) + ';\n'
                'wire clock_root, clock_leaf;\n'
                'sg13g2_buf_1 clkbuf_0_clk (.A(clk), .X(clock_root));\n'
                'sg13g2_buf_1 clkbuf_leaf_0_clk (.A(clock_root), .X(clock_leaf));\n'
                'sg13g2_dfrbpq_1 state (.CLK(clock_leaf), .RESET_B(reset), .D(y), .Q(q));\n' +
                ''.join(f'sg13g2_buf_1 place{100 + i} (.A({a}), .X({b}));\n'
                        for i, (a, b) in enumerate(zip(nets, nets[1:]))) + 'endmodule\n')
        (fixture / 'input.sdc').write_text('''create_clock -name sample_clock -period 3 [get_ports clk]
set_input_transition 0.1 [all_inputs]
set_case_analysis 1 [get_ports reset]
set_false_path -from [get_ports reset]
set_load 0.08 [get_ports y]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
''')
        if args.rebuffer:
            sdc = fixture / 'input.sdc'
            sdc.write_text(sdc.read_text().replace('-period 3 ', '-period 2 '))
        prepare = work / 'prepare.tcl'
        prepare.write_text('''set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set fixture $::env(RECEIVER_FIXTURE)
set_thread_count 1
read_lef "$lib/lef/sg13g2_tech.lef"
read_lef "$lib/lef/sg13g2_stdcell.lef"
define_corners slow fast
read_liberty -corner slow "$lib/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_verilog "$fixture/test.v"
link_design PCSReceiver
read_sdc "$fixture/input.sdc"
initialize_floorplan -die_area {0 0 200 200} -core_area {20 20 180 180} -site CoreSite
make_tracks
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.55
detailed_placement
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
global_route -guide_file "$fixture/core.guide" -congestion_iterations 5
estimate_parasitics -global_routing -spef_file "$fixture/core.spef"
file copy "$fixture/core_slow.spef" "$fixture/core.spef"
detailed_route -droute_end_iter 1
write_db "$fixture/core.odb"
exit
''')
        models = work / 'cells.v'
        models.write_text(''.join(f'module sg13g2_buf_{s}(A, X); input A; output X; assign X=A; endmodule\n'
            for s in (1, 2, 4, 8, 16)) + ''.join(
            f'module sg13g2_dfrbpq_{s}(CLK, D, Q, RESET_B); input CLK, D, RESET_B; output Q; reg Q; '
            'always @(posedge CLK or negedge RESET_B) if (!RESET_B) Q <= 0; else Q <= D; endmodule\n'
            for s in (1, 2)) + 'module sg13g2_a221oi_1(A1, A2, B1, B2, C1, Y); '
            'input A1, A2, B1, B2, C1; output Y; assign Y = ~((A1 & A2) | (B1 & B2) | C1); endmodule\n')
        environment = dict(os.environ, REPEATER_PDK=str(args.pdk.resolve()),
            RECEIVER_FIXTURE=str(fixture), REPEATER_WORK=str(output),
            RECEIVER_SEED=str(fixture / 'core.odb'), RECEIVER_SPEF=str(fixture / 'core.spef'),
            RECEIVER_SDC=str(fixture / 'input.sdc'), RECEIVER_SOURCE=str(HERE),
            RECEIVER_SLEW_MARGIN='0', RECEIVER_CAP_MARGIN='0',
            RECEIVER_REBUFFER='1' if args.rebuffer else '0',
            RECEIVER_DATA_SIZING='1' if args.rebuffer else '0',
            RECEIVER_TRIAL_LIMIT='12',
            RECEIVER_PYTHON=sys.executable, RECEIVER_CELLS=str(models),
            REPEATER_SOURCE=str(HERE.parents[1] / 'pdk/1G_repeater_IHPSG13G2'), OMP_NUM_THREADS='1')
        if owned:
            environment[SLOT_OWNER] = str(os.getpid())
        refresh = work / 'refresh.tcl'
        refresh.write_text('''set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set fixture $::env(RECEIVER_FIXTURE)
set work $::env(REPEATER_WORK)
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$lib/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$fixture/core.odb"
read_sdc "$fixture/input.sdc"
set_propagated_clock [all_clocks]
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
global_route
estimate_parasitics -global_routing
set baseline [sta::worst_slack_cmd max]
replace_cell place100 sg13g2_buf_16
set stale [sta::worst_slack_cmd max]
estimate_parasitics -global_routing
set fresh [sta::worst_slack_cmd max]
if {abs($stale - $fresh) < 1e-12} { error "fixture did not expose stale reduced RC" }
replace_cell place100 sg13g2_buf_1
estimate_parasitics -global_routing
source "$::env(RECEIVER_SOURCE)/receiver_timing_tradeoffs.tcl"
# An impossible hold target forces rejection, allowing the final rollback
# model to be checked independently of the candidate's timing improvement.
receiver_timing_tradeoffs {{place100 sg13g2_buf_16}} "$work/refresh_trials.csv" 1000 global_routing
set f [open "$work/refresh_trials.csv" r]
set rows [split [string trim [read $f]] \\n]
close $f
set values [split [lindex $rows 1] ,]
if {[lindex $values 6] != 0} { error "impossible hold guard accepted a trial" }
if {abs([lindex $values 3] * 1e-9 - $fresh) > 1e-15} {
    error "trial decision used stale global RC"
}
if {[[[[ord::get_db_block] findInst place100] getMaster] getName] != "sg13g2_buf_1"} {
    error "rejected trial did not restore the original cell"
}
if {abs([sta::worst_slack_cmd max] - $baseline) > 1e-15} {
    error "rejected trial did not restore baseline timing"
}
estimate_parasitics -global_routing
if {abs([sta::worst_slack_cmd max] - $baseline) > 1e-15} {
    error "restored timing disagrees with independent RC rebuild"
}
puts "PASS refreshed trial RC and rejected-cell rollback"
# Exercise the production hold-delay trimming helper too. An impossible hold
# requirement forces rejection, so both trial loads and rollback are checked.
replace_cell place100 sg13g2_dlygate4sd3_1
detailed_placement
global_route
estimate_parasitics -placement
estimate_parasitics -global_routing
set_clock_uncertainty -hold 100 [all_clocks]
set trim_baseline [sta::worst_slack_cmd max]
set trim_trials 0
rename replace_cell original_replace_cell
proc replace_cell {name target} {
    incr ::trim_trials
    original_replace_cell $name $target
}
rename sta::worst_slack_cmd sta::original_worst_slack_cmd
proc sta::worst_slack_cmd {mode} {
    set before [sta::original_worst_slack_cmd $mode]
    estimate_parasitics -global_routing
    set after [sta::original_worst_slack_cmd $mode]
    if {abs($before - $after) > 1e-15} {error "hold-cell decision used stale reduced RC"}
    return $before
}
source "$::env(REPEATER_SOURCE)/finish_timing.tcl"
trim_critical_hold_cells
if {$trim_trials < 2} {error "fixture did not exercise a rejected hold-cell trial"}
if {[[[[ord::get_db_block] findInst place100] getMaster] getName] != "sg13g2_dlygate4sd3_1"} {
    error "hold trimming failed to restore the rejected delay cell"
}
if {abs([sta::worst_slack_cmd max] - $trim_baseline) > 1e-12} {
    error "hold trimming failed to restore baseline timing"
}
puts "PASS production hold-cell trimming uses fresh RC for trials and rollback"
exit
''')
        stages = [('prepare', prepare)]
        if args.rebuffer:
            stages.append(('refresh', refresh))
        stages.append(('repair', HERE / 'receiver_final_repair.tcl'))
        for name, script in stages:
            log = work / (name + '.log')
            with log.open('w') as stream:
                result = execute(['prlimit', '--as=1073741824', '--core=0', '--',
                    args.openroad.resolve(), '-no_init', '-exit', script], cwd=work, env=environment,
                    stdout=stream, stderr=-2, timeout=90)
            if result.returncode:
                raise ValueError(name + ' failed:\n' + log.read_text())
            if name == 'repair':
                require_complete_routes(log.read_text())
                (output / 'global.log').write_text(log.read_text())
        if args.isolate:
            proof = rebuffer_compare(output / 'before.v', output / 'global.v', models)
            cells = parse(output / 'global.v')['cells']
            if not proof['inserted_buffers'] or not any(name.startswith('finish_slew_') for name in cells):
                raise ValueError('fixture did not exercise explicit weak-output isolation:\n' +
                                 (output / 'global.log').read_text())
            if 'VIOLATED' not in (output / 'initial_0_before_electrical.rpt').read_text():
                raise ValueError('fixture did not expose an overload before isolation')
            final_screen(output)
            print('PASS real single-strength isolation, exact endpoints and timing/electrical screens')
            return
        if args.rebuffer:
            proof = rebuffer_compare(output / 'before.v', output / 'global.v', models)
            if not (proof['inserted_buffers'] or proof['removed_buffers']):
                raise ValueError('native fixture did not exercise a buffer topology change:\n' +
                                 (output / 'global.log').read_text())
            if len((output / 'clock_trials.csv').read_text().splitlines()) <= 1:
                raise ValueError('fixture did not exercise clock replacement and estimator reset')
            for round in (1, 2):
                if len((output / f'data_{round}_trials.csv').read_text().splitlines()) <= 1:
                    raise ValueError('fixture did not exercise equivalent data-drive trials')
            final_screen(output)
            print('PASS native buffer replacement, exact endpoints, final route coverage, setup and hold')
            return
        before, after = parse(output / 'before.v'), parse(output / 'global.v')
        if before['interface'] != after['interface']:
            raise ValueError('native repair changed the register-loop interface')
        old, new = before['cells'], after['cells']
        if set(old) - set(new):
            raise ValueError('native repair removed a fixture cell')
        added = set(new) - set(old)
        replacements = 0
        for name, (master, pins) in old.items():
            target, new_pins = new[name]
            if master != target:
                if master not in BUFFERS or target not in BUFFERS:
                    raise ValueError('fixture repair changed non-buffer logic')
                replacements += 1
                new[name] = (master, new_pins)
        if not (added or replacements):
            raise ValueError('fixture did not exercise electrical repair')
        if connectivity(before, set()) != connectivity(after, added):
            raise ValueError('native repair changed the register-loop connectivity')
        final_screen(output)
        for round in (1, 2):
            if not (output / f'round_{round}_setup.rpt').is_file():
                raise ValueError('native repair omitted an optimization round')
        print('PASS native post-sizing electrical repair, endpoint preservation, both timing/electrical screens')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openroad', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--rebuffer', action='store_true')
    modes.add_argument('--isolate', action='store_true')
    run(parser.parse_args())
