"""Prepare real loaded IHP cells before entering global-route repair."""
import argparse
import os
from pathlib import Path
import re
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
COMMON = HERE.parents[1] / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(COMMON))
from rebuffer_equivalence import compare
from tool_process import configure_cleanup, execute


def run(args):
    with tempfile.TemporaryDirectory(prefix='receiver-prepare-') as directory:
        work = Path(directory)
        (work / 'test.v').write_text('''module PCSReceiver(input clk, input reset, output y);
wire q, n1, n2;
sg13g2_dfrbpq_1 state (.CLK(clk), .RESET_B(reset), .D(y), .Q(q));
sg13g2_buf_1 first (.A(q), .X(n1));
sg13g2_buf_1 second (.A(n1), .X(n2));
sg13g2_buf_1 third (.A(n2), .X(y));
endmodule
''')
        (work / 'placed.sdc').write_text('''create_clock -name sample_clock -period 20 [get_ports clk]
set_input_transition 0.1 [all_inputs]
set_case_analysis 1 [get_ports reset]
set_false_path -from [get_ports reset]
set_load 0.08 [get_ports y]
set_max_transition 0.3 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
''')
        libraries = '''set work $::env(REPEATER_WORK)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
read_lef "$lib/lef/sg13g2_tech.lef"
read_lef "$lib/lef/sg13g2_stdcell.lef"
define_corners slow fast
read_liberty -corner slow "$lib/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
'''
        (work / 'fixture.tcl').write_text(libraries + '''read_verilog "$work/test.v"
link_design PCSReceiver
read_sdc "$work/placed.sdc"
initialize_floorplan -die_area {0 0 200 200} -core_area {20 20 180 180} -site CoreSite
make_tracks
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.55
detailed_placement
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
estimate_parasitics -placement
set_max_transition 0.4 [current_design]
report_check_types -max_slew -violators > "$work/before_electrical.rpt"
write_verilog "$work/before.v"
write_db "$work/placed.odb"
exit
''')
        (work / 'reload.tcl').write_text(libraries + '''read_db "$work/pre_route.odb"
read_sdc "$work/pre_route.sdc"
set_propagated_clock [all_clocks]
source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
apply_routing_capacitance_margin 2.5 "$lib/lef/sg13g2_tech.lef"
estimate_parasitics -placement
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/reloaded_electrical.rpt"
write_verilog "$work/after.v"
exit
''')
        environment = dict(os.environ, REPEATER_WORK=str(work), REPEATER_TOP='PCSReceiver',
            REPEATER_PDK=str(args.pdk.resolve()), REPEATER_SOURCE=str(COMMON),
            REPEATER_STAGE='prepare_route', REPEATER_ROUTING_CAPACITANCE_MARGIN='2.5',
            REPEATER_ROUTE_TRANSITION_TARGET='0.4', REPEATER_ROUTE_CAP_MARGIN='30',
            REPEATER_PRE_ROUTE_SETUP_MARGIN='0.35', REPEATER_FINAL_HOLD_MARGIN='0.15')
        for name, script in [('fixture', work / 'fixture.tcl'),
                             ('prepare', COMMON / 'route_extract.tcl'),
                             ('reload', work / 'reload.tcl')]:
            with (work / (name + '.log')).open('w') as log:
                result = execute([str(args.openroad.resolve()), '-no_init', '-exit', str(script)],
                    cwd=work, env=environment, stdout=log, stderr=-2, timeout=90)
            if result.returncode:
                raise ValueError(name + ' failed:\n' + (work / (name + '.log')).read_text())
        if 'VIOLATED' not in (work / 'before_electrical.rpt').read_text():
            raise ValueError('fixture did not exercise an actual slew violation')
        for name in ('pre_route_electrical.rpt', 'reloaded_electrical.rpt'):
            if 'VIOLATED' in (work / name).read_text():
                raise ValueError('placement preparation left electrical violations:\n' + (work / name).read_text())
        transitions = re.findall(r'^set_max_transition\s+(\S+)',
                                 (work / 'pre_route.sdc').read_text(), re.M)
        if len(transitions) != 1 or abs(float(transitions[0]) - .4) > 1e-6:
            raise ValueError('preparation did not retain the routing slew target')
        models = work / 'cells.v'
        models.write_text(''.join(
            f'module {cell}(A, X); input A; output X; assign X=A; endmodule\n'
            for cell in [f'sg13g2_buf_{s}' for s in (1, 2, 4, 8, 16)] +
                        [f'sg13g2_dlygate4sd{s}_1' for s in (1, 2, 3)]) + ''.join(
            f'module sg13g2_dfrbpq_{s}(CLK, D, Q, RESET_B); input CLK, D, RESET_B; output Q; reg Q; '
            'always @(posedge CLK or negedge RESET_B) if (!RESET_B) Q<=0; else Q<=D; endmodule\n'
            for s in (1, 2)))
        proof = compare(work / 'before.v', work / 'after.v', models)
        if not (proof['inserted_buffers'] or proof['removed_buffers'] or proof['replacements']):
            raise ValueError('fixture did not exercise an electrical repair')
        print('PASS actual slew repair before routing, fresh RC reload and unchanged functional endpoints')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openroad', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    run(parser.parse_args())
