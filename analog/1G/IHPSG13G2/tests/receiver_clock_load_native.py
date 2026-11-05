"""Real routed clock-load repair must clear capacitance and preserve endpoints."""
import argparse
import csv
import os
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from receiver_final_repair import final_screen
from rebuffer_equivalence import compare
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    with memory_slot(['receiver-clock-load-native'], exclusive=True) as owned, \
            tempfile.TemporaryDirectory(prefix='receiver-clock-load-') as directory:
        root = Path(directory)
        work = root / 'repair'
        work.mkdir()
        design = ['module PCSReceiver(input clk, input reset); wire [3:0] q;',
                  'wire source_clock;', 'wire [3:0] leaf_clock, d;',
                  'sg13g2_buf_8 source (.A(clk), .X(source_clock));']
        for i in range(4):
            design += [f'sg13g2_buf_8 leaf{i} (.A(source_clock), .X(leaf_clock[{i}]));',
                       f'sg13g2_dfrbpq_1 flop{i} (.CLK(leaf_clock[{i}]), .RESET_B(reset), .D(d[{i}]), .Q(q[{i}]));',
                       f'sg13g2_dlygate4sd2_1 delay{i} (.A(q[{i}]), .X(d[{i}]));']
        design.append('endmodule')
        if args.direct_register:
            design = [line for line in design if not line.startswith('sg13g2_buf_8 leaf3 ')]
            design = [line.replace('.CLK(leaf_clock[3])', '.CLK(source_clock)') for line in design]
        (root / 'test.v').write_text('\n'.join(design) + '\n')
        (root / 'input.sdc').write_text('''create_clock -name sample_clock -period 5 [get_ports clk]
set_input_transition 0.1 [all_inputs]
set_case_analysis 1 [get_ports reset]
set_false_path -from [get_ports reset]
''')
        (root / 'prepare.tcl').write_text('''set fixture $::env(RECEIVER_FIXTURE)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
read_lef "$lib/lef/sg13g2_tech.lef"
read_lef "$lib/lef/sg13g2_stdcell.lef"
read_liberty "$lib/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_verilog "$fixture/test.v"
link_design PCSReceiver
read_sdc "$fixture/input.sdc"
initialize_floorplan -die_area {0 0 400 400} -core_area {10 10 390 390} -site CoreSite
make_tracks
place_pins -hor_layers Metal3 -ver_layers Metal2
set block [ord::get_db_block]
set units [$block getDbUnitsPerMicron]
foreach row {{source 192 192} {leaf0 48 48} {leaf1 48 336} {leaf2 336 48} {leaf3 336 336}
             {flop0 60 48} {flop1 60 336} {flop2 348 48} {flop3 348 336}
             {delay0 76 48} {delay1 76 336} {delay2 364 48} {delay3 364 336}} {
    lassign $row name x y
    set inst [$block findInst $name]
    if {$inst == "NULL"} {continue}
    $inst setLocation [expr {$x * $units}] [expr {$y * $units}]
    $inst setPlacementStatus PLACED
}
detailed_placement
check_placement -verbose
write_db "$fixture/seed.odb"
exit
''')
        models = root / 'cells.v'
        models.write_text(''.join(f'module sg13g2_buf_{i}(A, X); input A; output X; assign X = A; endmodule\n'
            for i in (1, 2, 4, 8, 16)) + '''module sg13g2_dlygate4sd2_1(A, X);
input A; output X; assign X = A; endmodule
module sg13g2_dfrbpq_1(CLK, D, RESET_B, Q);
input CLK, D, RESET_B; output Q; reg Q;
always @(posedge CLK or negedge RESET_B) if (!RESET_B) Q <= 0; else Q <= D;
endmodule
''')
        environment = dict(os.environ, REPEATER_WORK=str(work), RECEIVER_FIXTURE=str(root),
            REPEATER_PDK=str(args.pdk.resolve()), RECEIVER_SOURCE=str(HERE),
            REPEATER_SOURCE=str(HERE.parents[1] / 'pdk/1G_repeater_IHPSG13G2'),
            RECEIVER_SEED=str(root / 'seed.odb'), RECEIVER_SDC=str(root / 'input.sdc'), OMP_NUM_THREADS='1')
        if owned:
            environment[SLOT_OWNER] = str(os.getpid())
        # The guard fixture attempts to buffer a data net. It must reject the
        # entire plan before adding or changing any instance or net.
        stage = (HERE / 'receiver_clock_load_stage.tcl').read_text()
        prefix = stage.split('report_check_types -max_capacitance -violators')[0]
        (root / 'guards.tcl').write_text(prefix + '''set block [ord::get_db_block]
set before_cells [llength [$block getInsts]]
set before_nets [llength [$block getNets]]
if {![catch {receiver_clock_loads {delay0/X} "$work/invalid.csv" invalid} message]} {
    error "data net accepted as a clock"
}
if {![string match "unprotected internal clock net required*" $message]} {error $message}
if {[llength [$block getInsts]] != $before_cells || [llength [$block getNets]] != $before_nets} {
    error "guard changed the input design"
}
puts "PASS clock-only preflight rejected data net without mutation"
exit
''')
        for name, script in (('prepare', root / 'prepare.tcl'), ('guards', root / 'guards.tcl'),
                             ('global', HERE / 'receiver_clock_load_stage.tcl')):
            log = work / (name + '.log')
            with log.open('w') as stream:
                result = execute(['prlimit', '--as=1073741824', '--core=0', '--',
                    args.openroad.resolve(), '-no_init', '-exit', script],
                    env=environment, stdout=stream, stderr=-2, timeout=90)
            if result.returncode:
                raise ValueError(name + ' failed:\n' + log.read_text())
        before = (work / 'before_capacitance.rpt').read_text()
        if 'source/X' not in before or 'VIOLATED' not in before:
            raise ValueError('fixture did not overload its source clock:\n' + before)
        proof = compare(work / 'before.v', work / 'global.v', models)
        with (work / 'clock_loads.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != 4 or len(proof['inserted_buffers']) < 4 or proof['removed_buffers']:
            raise ValueError('expected four clock branches and unchanged original cells')
        try:
            final_screen(work)
        except ValueError as error:
            raise ValueError(str(error) + '\n' + (work / 'before_capacitance.rpt').read_text() +
                             '\nAfter:\n' + (work / 'margin_electrical.rpt').read_text()) from error
        print('PASS IHP clock-load isolation: four midpoint buffers, exact endpoints, '
              'clock-only guard and both timing/electrical screens')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openroad', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--direct-register', action='store_true')
    run(parser.parse_args())
