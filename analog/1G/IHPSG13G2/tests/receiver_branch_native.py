"""Real branched buffer removal must preserve sibling loads and improve timing."""
import argparse
import os
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from receiver_final_repair import final_screen
from receiver_path_sizing import slack
from rebuffer_equivalence import compare
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    with memory_slot(['receiver-branch-native'], exclusive=True) as owned, \
            tempfile.TemporaryDirectory(prefix='receiver-branch-') as directory:
        root = Path(directory)
        work = root / 'repair'
        work.mkdir()
        # Each candidate input also drives a sibling register. Earlier removal
        # required an input with only one load and therefore missed this chain.
        wires = ['root'] + [f'n{i}' for i in range(4)]
        text = 'module PCSReceiver(input clk, input reset, output q, output [4:0] taps);\n'
        text += 'wire ' + ', '.join(wires) + ';\n'
        text += 'sg13g2_dfrbpq_1 state (.CLK(clk), .RESET_B(reset), .D(n3), .Q(q));\n'
        text += 'sg13g2_buf_16 driver (.A(q), .X(root));\n'
        for i, (a, b) in enumerate(zip(wires, wires[1:])):
            text += f'sg13g2_buf_8 place{100 + i} (.A({a}), .X({b}));\n'
        for i, net in enumerate(wires):
            text += f'sg13g2_dfrbpq_1 sibling{i} (.CLK(clk), .RESET_B(reset), .D({net}), .Q(taps[{i}]));\n'
        text += 'endmodule\n'
        (root / 'test.v').write_text(text)
        (root / 'input.sdc').write_text('''create_clock -name sample_clock -period 1.1 [get_ports clk]
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
source "$::env(RECEIVER_SOURCE)/receiver_branch_buffers.tcl"
set block [ord::get_db_block]
set branch [$block findInst place100]
set input [[$branch findITerm A] getNet]
$branch setDoNotTouch 1
if {[receiver_branch_buffers {place100} "$fixture/guard.csv"] != 0} { error "protected branch removed" }
$branch setDoNotTouch 0
$input setSigType CLOCK
if {[receiver_branch_buffers {place100} "$fixture/guard.csv"] != 0} { error "clock branch removed" }
$input setSigType SIGNAL
set extras {}
for {set i 0} {$i < 6} {incr i} {
    set extra [odb::dbInst_create $block [$branch getMaster] guard$i]
    [$extra findITerm A] connect $input
    lappend extras $extra
}
if {[receiver_branch_buffers {place100} "$fixture/guard.csv"] != 0} { error "excess merged fanout accepted" }
foreach extra $extras { odb::dbInst_destroy $extra }
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
            for i in (1, 2)) + 'module sg13g2_a221oi_1(A1, A2, B1, B2, C1, Y); '
            'input A1, A2, B1, B2, C1; output Y; assign Y = ~((A1 & A2) | (B1 & B2) | C1); endmodule\n')
        environment = dict(os.environ, REPEATER_WORK=str(work), RECEIVER_FIXTURE=str(root),
            REPEATER_PDK=str(args.pdk.resolve()), RECEIVER_SOURCE=str(HERE),
            REPEATER_SOURCE=str(HERE.parents[1] / 'pdk/1G_repeater_IHPSG13G2'),
            RECEIVER_SEED=str(root / 'seed.odb'), RECEIVER_SDC=str(root / 'input.sdc'),
            RECEIVER_BRANCH_ROUNDS='6', RECEIVER_BRANCH_SPAN='120',
            OMP_NUM_THREADS='1')
        if owned:
            environment[SLOT_OWNER] = str(os.getpid())
        for name, script in (('prepare', root / 'prepare.tcl'), ('global', HERE / 'receiver_branch_stage.tcl')):
            log = work / (name + '.log')
            with log.open('w') as stream:
                result = execute(['prlimit', '--as=1073741824', '--core=0', '--',
                    args.openroad.resolve(), '-no_init', '-exit', script],
                    env=environment, stdout=stream, stderr=-2, timeout=90)
            if result.returncode:
                raise ValueError(name + ' failed:\n' + log.read_text())
        proof = compare(work / 'before.v', work / 'global.v', models)
        if len(proof['removed_buffers']) < 2:
            detail = (work / 'before_setup.rpt').read_text() + (work / 'global.log').read_text()[-4000:]
            raise ValueError('fixture did not remove multiple branched buffers:\n' + detail)
        journals = ''.join(p.read_text() for p in work.glob('branches_*.csv'))
        if not any(line.startswith('place') for line in journals.splitlines()):
            raise ValueError('missing explicit branch selections')
        before, after = (slack(work / (name + '_setup.rpt'), 'max') for name in ('before', 'margin'))
        if after < before + .05:
            raise ValueError(f'branch removal did not improve setup by 50 ps: {before} -> {after}')
        final_screen(work)
        print(f'PASS real IHP branch removal: setup {before:.9f} -> {after:.9f} ns, '
              'exact endpoints and both timing/electrical screens')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openroad', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    run(parser.parse_args())
