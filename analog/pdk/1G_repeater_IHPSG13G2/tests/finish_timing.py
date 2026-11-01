#!/usr/bin/env python3
"""Repair an actual IHP hold failure and overloaded single-drive gate."""
import argparse
import re
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1]


def check(args):
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    with tempfile.TemporaryDirectory(prefix='cpphdl-final-timing-') as temporary:
        work = Path(temporary)
        design = ['module TimingRepair(input clk, A, B, output [8:0] Y);',
                  'wire q, weak_net;',
                  'sg13g2_dfrbp_1 launch(.CLK(clk), .D(A), .RESET_B(B), .Q(q), .Q_N());',
                  'sg13g2_xnor2_1 weak_driver(.A(q), .B(B), .Y(weak_net));']
        for index in range(8):
            design.extend([f'wire data{index};',
                f'sg13g2_buf_16 load_buffer{index}(.A(weak_net), .X(data{index}));',
                f'sg13g2_dfrbp_1 capture{index}(.CLK(clk), .D(data{index}), '
                f'.RESET_B(B), .Q(Y[{index}]), .Q_N());'])
        design.extend(['sg13g2_dfrbp_1 short_path(.CLK(clk), .D(q), '
                       '.RESET_B(B), .Q(Y[8]), .Q_N());', 'endmodule'])
        (work / 'design.v').write_text('\n'.join(design) + '\n')
        loaded = bool(args.clock_finish)
        final_repair = (f'source {{{args.clock_finish.resolve()}}}\nfinish_processing_clock_timing {{{work}}}'
                        if loaded else f'finish_routed_timing {{{work}}}')
        margin_setup = f'''source {{{SOURCE}/routing_rc_margin.tcl}}
set ::env(REPEATER_PDK) {{{args.pdk.resolve()}}}
set ::env(REPEATER_ROUTING_CAPACITANCE_MARGIN) 2.5
apply_routing_capacitance_margin 2.5 {{{library}/lef/sg13g2_tech.lef}}
'''
        restore_check = f'''estimate_parasitics -global_routing -spef_file {{{work}/finished.spef}}
apply_routing_capacitance_margin 1 {{{library}/lef/sg13g2_tech.lef}}
estimate_parasitics -global_routing -spef_file {{{work}/nominal.spef}}
'''
        script = f'''set_thread_count 1
read_lef {{{library}/lef/sg13g2_tech.lef}}
read_lef {{{library}/lef/sg13g2_stdcell.lef}}
define_corners slow fast
read_liberty -corner slow {{{library}/lib/sg13g2_stdcell_slow_1p08V_125C.lib}}
read_liberty -corner fast {{{library}/lib/sg13g2_stdcell_fast_1p32V_m40C.lib}}
read_verilog {{{work}/design.v}}
link_design TimingRepair
initialize_floorplan -die_area {{0 0 80 80}} -core_area {{10 10 70 70}} -site CoreSite
create_clock -period 20 [get_ports clk]
# Deliberately stress the short path; this is not the receiver's constraint.
set_clock_uncertainty 0.2 [all_clocks]
set_input_delay 0.4 -clock clk [get_ports {{A B}}]
set_output_delay 0.4 -clock clk [get_ports Y*]
set_input_transition 0.1 [all_inputs]
set_load {0.08 if loaded else 0.005} [all_outputs]
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
{margin_setup}
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
add_global_connection -net VDD -inst_pattern .* -pin_pattern VDD -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern VSS -ground
global_connect
make_tracks
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.3
detailed_placement
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
global_route
estimate_parasitics -global_routing
if {{[sta::worst_slack_cmd min] >= 0}} {{error "fixture no longer exercises a hold failure"}}
report_check_types -max_slew -violators > {{{work}/before.rpt}}
source {{{SOURCE}/finish_timing.tcl}}
# Exercise buffer insertion with real placement and parasitic state. Calling
# it before the placement grid exists used to crash OpenROAD.
if {{[isolate_single_strength_outputs {{{work}/before.rpt}} smoke] != 1}} {{
    error "fixture no longer exercises the single-drive output repair"
}}
finish_route_update
# Exercise direct cell edits, antenna/power connections and RC rebuild ordering.
{final_repair}
{restore_check}
check_placement -verbose
if {{[sta::worst_slack_cmd min] < 0.05e-9}} {{error "hold reserve was lost"}}
puts "PASS: actual hold and slew failures repaired with legal IHP cells"
exit
'''
        (work / 'test.tcl').write_text(script)
        result = subprocess.run([str(args.tools.resolve() / 'bin/openroad'), '-no_init', '-exit',
                                 str(work / 'test.tcl')], capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        if result.returncode or '[ERROR' in output or 'PASS:' not in output:
            raise ValueError(output)
        for name in ('global_electrical.rpt', 'global_setup.rpt', 'global_hold.rpt',
                     'finished_global.odb', 'finished_global.sdc'):
            if not (work / name).is_file():
                raise ValueError('incomplete repair output: ' + name)
        if 'VIOLATED' in (work / 'global_electrical.rpt').read_text():
            raise ValueError('electrical violation survived repair')
        files = sorted(work.glob('finished*.spef'))
        if not files or not (work / 'optimization_setup.rpt').is_file():
            raise ValueError('missing optimization/nominal evidence')
        for path in files:
            nominal = work / path.name.replace('finished', 'nominal', 1)
            def caps(file):
                return dict(re.findall(r'^\*D_NET (\S+) (\S+)', file.read_text(), re.M))
            actual, expected = caps(path), caps(nominal)
            if not actual or actual != expected:
                raise ValueError('optimization margin leaked into final timing')
        print(output[output.index('PASS:'):].strip())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--clock-finish', type=Path, help='exercise the loaded admission-output repair policy')
    check(parser.parse_args())
