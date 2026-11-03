"""Real IHP pin/hold recovery must insert delays without changing logic."""
import argparse
import os
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from receiver_pin_stage import SCRIPT
from receiver_final_repair import final_screen
from receiver_path_sizing import slack
from pin_swap_equivalence import compare as pin_compare
from rebuffer_equivalence import compare as buffer_compare
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    with memory_slot(['receiver-pin-hold-native'], exclusive=True) as owned, \
            tempfile.TemporaryDirectory(prefix='receiver-pin-hold-') as directory:
        root = Path(directory)
        work = root / 'repair'
        work.mkdir()
        (root / 'test.v').write_text('''module PCSReceiver(input clk, input reset, output q1, output q2);
sg13g2_dfrbpq_1 first (.CLK(clk), .RESET_B(reset), .D(q2), .Q(q1));
sg13g2_dfrbpq_1 second (.CLK(clk), .RESET_B(reset), .D(q1), .Q(q2));
endmodule
''')
        (root / 'input.sdc').write_text('''create_clock -name sample_clock -period 3 [get_ports clk]
set_clock_uncertainty -hold 0.4 [get_clocks sample_clock]
set_input_transition 0.1 [all_inputs]
set_case_analysis 1 [get_ports reset]
set_false_path -from [get_ports reset]
set_load 0.005 [all_outputs]
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
initialize_floorplan -die_area {0 0 100 100} -core_area {10 10 90 90} -site CoreSite
make_tracks
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.55
detailed_placement
check_placement -verbose
write_db "$fixture/seed.odb"
exit
''')
        models = root / 'cells.v'
        models.write_text(''.join(f'module sg13g2_buf_{i}(A, X); input A; output X; assign X = A; endmodule\n'
            for i in (1, 2, 4, 8, 16)) + ''.join(
            f'module sg13g2_dlygate4sd{i}_1(A, X); input A; output X; assign X = A; endmodule\n'
            for i in (1, 2, 3)) + ''.join(
            f'module sg13g2_dfrbpq_{i}(CLK, D, RESET_B, Q); input CLK, D, RESET_B; output Q; reg Q; '
            'always @(posedge CLK or negedge RESET_B) if (!RESET_B) Q <= 0; else Q <= D; endmodule\n'
            for i in (1, 2)))
        (root / 'repair.tcl').write_text(SCRIPT)
        environment = dict(os.environ, REPEATER_WORK=str(work), RECEIVER_FIXTURE=str(root),
            REPEATER_PDK=str(args.pdk.resolve()), RECEIVER_SOURCE=str(HERE),
            REPEATER_SOURCE=str(HERE.parents[1] / 'pdk/1G_repeater_IHPSG13G2'),
            RECEIVER_SEED=str(root / 'seed.odb'), RECEIVER_SDC=str(root / 'input.sdc'),
            RECEIVER_REPAIR_HOLD='1', OMP_NUM_THREADS='1')
        if owned:
            environment[SLOT_OWNER] = str(os.getpid())
        for name in ('prepare', 'repair'):
            log = root / (name + '.log')
            with log.open('w') as stream:
                result = execute(['prlimit', '--as=1073741824', '--core=0', '--',
                    args.openroad.resolve(), '-no_init', '-exit', root / (name + '.tcl')],
                    env=environment, stdout=stream, stderr=-2, timeout=90)
            if result.returncode:
                raise ValueError(name + ' failed:\n' + log.read_text())
        (work / 'global.log').write_text((root / 'repair.log').read_text())
        if slack(work / 'hold_seed.rpt', 'min') >= 0:
            raise ValueError('fixture did not expose hold failure')
        pin_compare(work / 'before_pins.v', work / 'after_pins.v', models)
        proof = buffer_compare(work / 'after_pins.v', work / 'global.v', models)
        if not proof['inserted_buffers']:
            raise ValueError('hold recovery did not insert a physical delay')
        final_screen(work)
        print('PASS real IHP hold recovery, exact endpoints and both timing/electrical screens')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openroad', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    run(parser.parse_args())
