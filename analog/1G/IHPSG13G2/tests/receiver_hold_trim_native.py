"""Check accepted and rejected hold-delay edits with real IHP cells and routing."""
import argparse
import csv
import os
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(HERE), str(HERE.parents[1] / 'pdk/1G_repeater_IHPSG13G2')]
from rebuffer_equivalence import compare
from receiver_path_sizing import slack
from tool_process import configure_cleanup, execute


def run(args):
    with tempfile.TemporaryDirectory(prefix='receiver-hold-trim-') as temporary:
        work = Path(temporary)
        (work / 'input.v').write_text('''module PCSReceiver(input clk, reset, output q);
wire a, b, c;
sg13g2_dfrbpq_1 state(.CLK(clk), .RESET_B(reset), .D(c), .Q(q));
sg13g2_dlygate4sd3_1 delay0(.A(q), .X(a));
sg13g2_dlygate4sd3_1 delay1(.A(a), .X(b));
sg13g2_dlygate4sd3_1 delay2(.A(b), .X(c));
endmodule
''')
        models = work / 'cells.v'
        models.write_text(''.join(
            f'module {name}(A,X); input A; output X; assign X=A; endmodule\n'
            for name in ['sg13g2_buf_1'] + [f'sg13g2_dlygate4sd{i}_1' for i in (1, 2, 3)]) +
            'module sg13g2_dfrbpq_1(CLK,D,Q,RESET_B); input CLK,D,RESET_B; output Q; reg Q; '
            'always @(posedge CLK or negedge RESET_B) if (!RESET_B) Q<=0; else Q<=D; endmodule\n')
        library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
        common = f'''set_thread_count 1
define_corners slow fast
read_liberty -corner slow {{{library}/lib/sg13g2_stdcell_slow_1p08V_125C.lib}}
read_liberty -corner fast {{{library}/lib/sg13g2_stdcell_fast_1p32V_m40C.lib}}
'''
        rc = '''set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
global_route
estimate_parasitics -placement
estimate_parasitics -global_routing
'''
        (work / 'trial.tcl').write_text(common + f'''read_lef {{{library}/lef/sg13g2_tech.lef}}
read_lef {{{library}/lef/sg13g2_stdcell.lef}}
read_verilog {{{work}/input.v}}
link_design PCSReceiver
add_global_connection -net VDD -pin_pattern ^VDD$ -power
add_global_connection -net VSS -pin_pattern ^VSS$ -ground
global_connect
set_dont_touch [get_cells delay2]
initialize_floorplan -die_area {{0 0 80 80}} -core_area {{10 10 70 70}} -site CoreSite
make_tracks
create_clock -period 3 [get_ports clk]
set_input_transition 0.1 [all_inputs]
set_false_path -from [get_ports reset]
set_case_analysis 1 [get_ports reset]
set_load 0.005 [get_ports q]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.4
detailed_placement
{rc}
source {{{HERE}/receiver_hold_trim.tcl}}
write_verilog {{{work}/before.v}}
report_worst_slack -max -digits 9 > {{{work}/before_setup.rpt}}
set baseline [sta::worst_slack_cmd max]
foreach limits {{{{0 60}} {{201 60}} {{1 0}} {{1 901}}}} {{
    lassign $limits count seconds
    if {{![catch {{receiver_hold_trim 2 0.1 3 {{{work}/invalid.csv}} $count $seconds}}]}} {{
        error "invalid hold-trim budget accepted"
    }}
}}
set changed [receiver_hold_trim 2 10 3 {{{work}/rejected.csv}} 1 60]
if {{$changed != 0}} {{error "impossible hold reserve accepted a change"}}
write_verilog {{{work}/rejected.v}}
estimate_parasitics -global_routing
if {{abs([sta::worst_slack_cmd max] - $baseline) > 1e-12}} {{error "rollback did not restore timing"}}
set changed [receiver_hold_trim 2 0.1 3 {{{work}/accepted.csv}}]
if {{$changed == 0}} {{error "fixture did not exercise a delay reduction"}}
if {{[[[[ord::get_db_block] findInst delay2] getMaster] getName] != "sg13g2_dlygate4sd3_1"}} {{error "protected delay cell changed"}}
foreach name {{delay0 delay1 delay2}} {{
    set inst [[ord::get_db_block] findInst $name]
    foreach rail {{VDD VSS}} {{
        if {{[[[$inst findITerm $rail] getNet] getName] != $rail}} {{error "power connection changed"}}
    }}
}}
report_worst_slack -max -digits 9 > {{{work}/after_setup.rpt}}
report_worst_slack -min -digits 9 > {{{work}/after_hold.rpt}}
report_check_types -max_slew -max_capacitance -max_fanout -violators > {{{work}/electrical.rpt}}
write_verilog {{{work}/after.v}}
write_db {{{work}/after.odb}}
write_sdc {{{work}/after.sdc}}
exit
''')
        (work / 'reload.tcl').write_text(common + f'''read_db {{{work}/after.odb}}
read_sdc {{{work}/after.sdc}}
{rc}
report_worst_slack -max -digits 9 > {{{work}/reload_setup.rpt}}
report_worst_slack -min -digits 9 > {{{work}/reload_hold.rpt}}
exit
''')
        for name in ('trial', 'reload'):
            with (work / (name + '.log')).open('w') as log:
                result = execute([args.openroad.resolve(), '-no_init', '-exit', work / (name + '.tcl')],
                                 stdout=log, stderr=-2, timeout=90)
            output = (work / (name + '.log')).read_text()
            if result.returncode or any(s in output for s in ('EST-0026', 'EST-0027', 'Missing route to pin')):
                raise ValueError(output)
        with (work / 'rejected.csv').open() as stream:
            rejected = list(csv.DictReader(stream))
        if len(rejected) != 1 or any(row['accepted'] != '0' for row in rejected):
            raise ValueError('rejected-trial fixture did not exercise rollback')
        if (work / 'before.v').read_bytes() != (work / 'rejected.v').read_bytes():
            raise ValueError('rejected trials changed the netlist')
        proof = compare(work / 'before.v', work / 'after.v', models)
        if not proof['replacements'] or proof['removed_buffers'] or proof['inserted_buffers']:
            raise ValueError('expected only same-function delay-cell replacements')
        if slack(work / 'after_setup.rpt', 'max') <= slack(work / 'before_setup.rpt', 'max'):
            raise ValueError('delay reduction did not improve setup')
        if slack(work / 'after_hold.rpt', 'min') < .1 or 'VIOLATED' in (work / 'electrical.rpt').read_text():
            raise ValueError('lost hold/electrical margin')
        for kind, mode in (('setup', 'max'), ('hold', 'min')):
            if abs(slack(work / ('after_' + kind + '.rpt'), mode) -
                   slack(work / ('reload_' + kind + '.rpt'), mode)) > .001:
                raise ValueError('independent routed timing reload differs')
        print('PASS IHP hold trimming: delay reduction, rejected rollback, exact endpoints and fresh timing')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--openroad', type=Path, required=True)
    run(parser.parse_args())
