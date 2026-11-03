"""Real output isolation must improve fresh-routed timing and preserve logic."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from receiver_final_repair import final_screen
from receiver_path_sizing import slack
from receiver_output_stage import digest, unbuffer_origin
from rebuffer_equivalence import compare
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    with memory_slot(['receiver-output-native'], exclusive=True) as owned, \
            tempfile.TemporaryDirectory(prefix='receiver-output-') as directory:
        root = Path(directory)
        work = root / 'repair'
        work.mkdir()
        # Initially isolate only weak/Y. The second loop must become critical
        # and be selected from the refreshed report in the following round.
        (root / 'test.v').write_text('''module PCSReceiver(input clk, input reset, output y, output z);
wire q, q2;
sg13g2_dfrbpq_1 state (.CLK(clk), .RESET_B(reset), .D(y), .Q(q));
sg13g2_a221oi_1 weak (.A1(q), .A2(q), .B1(q), .B2(q), .C1(q), .Y(y));
sg13g2_dfrbpq_1 state2 (.CLK(clk), .RESET_B(reset), .D(z), .Q(q2));
sg13g2_a221oi_1 weak2 (.A1(q2), .A2(q2), .B1(q2), .B2(q2), .C1(q2), .Y(z));
endmodule
''')
        (root / 'input.sdc').write_text('''create_clock -name sample_clock -period 1.5 [get_ports clk]
set_input_transition 0.1 [all_inputs]
set_case_analysis 1 [get_ports reset]
set_false_path -from [get_ports reset]
set_load 0.04 [get_ports {y z}]
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
        (work / 'output_pins.tcl').write_text('set output_pins {weak/Y}\n')
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
            RECEIVER_OUTPUT_ROUNDS='3', RECEIVER_OUTPUT_LIMIT='128', RECEIVER_PYTHON=sys.executable,
            OMP_NUM_THREADS='1')
        if owned:
            environment[SLOT_OWNER] = str(os.getpid())
        for name, script in (('prepare', root / 'prepare.tcl'), ('global', HERE / 'receiver_output_stage.tcl')):
            log = work / (name + '.log')
            with log.open('w') as stream:
                result = execute(['prlimit', '--as=1073741824', '--core=0', '--',
                    args.openroad.resolve(), '-no_init', '-exit', script],
                    env=environment, stdout=stream, stderr=-2, timeout=90)
            if result.returncode:
                raise ValueError(name + ' failed:\n' + log.read_text())
        proof = compare(work / 'before.v', work / 'global.v', models)
        if not proof['inserted_buffers'] or 'receiver_output_0_0' not in (work / 'global.v').read_text():
            raise ValueError('fixture did not retain the explicit output buffer')
        journal = work / 'output_buffers_1.csv'
        if not journal.exists() or 'weak2/Y' not in journal.read_text():
            detail = '\n'.join(p.name + ': ' + p.read_text()[:2000]
                               for p in sorted(work.glob('*setup.rpt')))
            detail += '\n' + (work / 'global.v').read_text()
            raise ValueError('fixture did not reselect the newly critical second loop\n' + detail)
        before, after = (slack(work / (name + '_setup.rpt'), 'max') for name in ('before', 'margin'))
        if after < before + .05:
            raise ValueError(f'output buffering did not improve setup by 50 ps: {before} -> {after}')
        final_screen(work)
        # Exercise the reusable measurement and its repair handoff using this
        # actual routed IHP fixture. The fixture supplies a diagnostic receipt,
        # not a complete receiver qualification.
        diagnostic, measurement = root / 'diagnostic', root / 'measurement'
        diagnostic.mkdir()
        for saved, original in (('candidate.odb', work / 'global.odb'),
                                ('candidate.sdc', root / 'input.sdc'),
                                ('after.v', work / 'global.v')):
            os.link(original, diagnostic / saved)
        tools = args.openroad.resolve().parent.parent
        libroot = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
        inputs = [Path(__file__).resolve(), models, root / 'test.v', root / 'prepare.tcl',
                  tools / 'bin/openroad', tools / 'root/usr/bin/openroad']
        inputs += [libroot / ('lib/sg13g2_stdcell_' + corner + '.lib')
                   for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C')]
        producer = dict(status='diagnostic_complete', source_sha256={str(p): digest(p) for p in inputs},
            artifact_sha256={str(p): digest(p) for p in diagnostic.iterdir()},
            rounds=[dict(name='after', connectivity=proof, electrical_pass=True,
                setup_ns=slack(work / 'global_setup.rpt', 'max'),
                hold_ns=slack(work / 'global_hold.rpt', 'min'))],
            timing_qualified=False, physical_qualified=False, full_chip_qualified=False)
        (diagnostic / 'checks.json').write_text(json.dumps(producer, indent=2) + '\n')
        with (root / 'measurement.log').open('w') as stream:
            result = execute([sys.executable, HERE / 'receiver_global_measurement.py',
                '--origin', diagnostic, '--output', measurement,
                '--tools', tools, '--pdk', args.pdk.resolve()],
                env=environment, stdout=stream, stderr=-2, timeout=90)
        if result.returncode:
            raise ValueError('measurement failed:\n' + (root / 'measurement.log').read_text())
        measured = json.loads((measurement / 'checks.json').read_text())
        if (measured['status'] != 'global_margin_measurement_complete' or
                measured['optimization_screen_pass'] is not True):
            raise ValueError('fixture RC measurement did not pass: ' + str(measured))
        unbuffer_origin(diagnostic, measurement, producer)
        print(f'PASS real IHP output buffering: setup {before:.9f} -> {after:.9f} ns, '
              'exact endpoints, both timing/electrical screens and independent RC restoration')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openroad', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    run(parser.parse_args())
