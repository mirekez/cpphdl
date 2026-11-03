"""Exercise native equivalent-input repair, then prove the emitted netlists."""
import argparse
import os
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from pin_swap_equivalence import compare
from receiver_path_sizing import slack
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    with memory_slot(['receiver-pin-repair-native'], exclusive=True) as owned, \
            tempfile.TemporaryDirectory(prefix='receiver-pin-repair-') as directory:
        work = Path(directory)
        (work / 'test.v').write_text('''module PCSReceiver(input clk, input reset, input a, input b, output q1, output q2);
wire n1, n2;
sg13g2_nand2_1 gate1(.A(a), .B(b), .Y(n1));
sg13g2_nand2_1 gate2(.A(b), .B(a), .Y(n2));
sg13g2_dfrbpq_1 state1(.CLK(clk), .RESET_B(reset), .D(n1), .Q(q1));
sg13g2_dfrbpq_1 state2(.CLK(clk), .RESET_B(reset), .D(n2), .Q(q2));
endmodule
''')
        (work / 'input.sdc').write_text('''create_clock -name sample_clock -period 1.2 [get_ports clk]
set_input_delay -clock sample_clock 1.0 [get_ports a]
set_input_delay -clock sample_clock 0.1 [get_ports b]
set_output_delay -clock sample_clock 0.1 [get_ports {q1 q2}]
set_input_transition 0.1 [all_inputs]
set_case_analysis 1 [get_ports reset]
set_false_path -from [get_ports reset]
set_load 0.01 [all_outputs]
''')
        script = work / 'run.tcl'
        script.write_text('''set work $::env(REPEATER_WORK)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
read_lef "$lib/lef/sg13g2_tech.lef"
read_lef "$lib/lef/sg13g2_stdcell.lef"
define_corners slow fast
read_liberty -corner slow "$lib/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_verilog "$work/test.v"
link_design PCSReceiver
read_sdc "$work/input.sdc"
initialize_floorplan -die_area {0 0 100 100} -core_area {10 10 90 90} -site CoreSite
make_tracks
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.55
detailed_placement
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
global_route
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/before_setup.rpt"
source "$::env(RECEIVER_SOURCE)/receiver_pin_repair.tcl"
receiver_pin_repair $work
global_route
estimate_parasitics -placement
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/after_setup.rpt"
exit
''')
        # Models reproduce the actual IHP NAND and resettable register functions.
        # Only changed combinational cells are evaluated by the pin-swap proof.
        models = work / 'cells.v'
        models.write_text('''module sg13g2_nand2_1(A,B,Y); input A,B; output Y; assign Y=~(A&B); endmodule
module sg13g2_dfrbpq_1(CLK,D,Q,RESET_B); input CLK,D,RESET_B; output Q; reg Q;
always @(posedge CLK or negedge RESET_B) if (!RESET_B) Q<=0; else Q<=D; endmodule
''')
        environment = dict(os.environ, REPEATER_WORK=str(work), RECEIVER_SOURCE=str(HERE),
                           REPEATER_PDK=str(args.pdk.resolve()), OMP_NUM_THREADS='1')
        if owned:
            environment[SLOT_OWNER] = str(os.getpid())
        log = work / 'native.log'
        with log.open('w') as stream:
            result = execute(['prlimit', '--as=1073741824', '--core=0', '--',
                args.openroad.resolve(), '-no_init', '-exit', script], env=environment,
                stdout=stream, stderr=-2, timeout=90)
        if result.returncode:
            raise ValueError('native pin repair failed:\n' + log.read_text())
        proof = compare(work / 'before_pins.v', work / 'after_pins.v', models)
        if not proof['changed_cells']:
            raise ValueError('native fixture did not exercise pin repair:\n' + log.read_text())
        before, after = (slack(work / (prefix + '_setup.rpt'), 'max') for prefix in ('before', 'after'))
        if after < before - .001:
            raise ValueError(f'pin repair worsened fresh-routed setup: {before} -> {after}')
        print(f'PASS native equivalent-input repair: {len(proof["changed_cells"])} cells, '
              f'fresh-routed setup {before:.9f} -> {after:.9f} ns')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--openroad', type=Path, required=True)
    run(parser.parse_args())
