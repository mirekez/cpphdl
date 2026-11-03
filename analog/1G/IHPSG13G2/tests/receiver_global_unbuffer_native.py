"""Prove short-buffer removal on an IHP register loop with fresh routed timing."""
import argparse
import csv
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'pdk/1G_repeater_IHPSG13G2'))
from buffer_equivalence import compare
from rebuffer_equivalence import compare as compare_rebuffer
from receiver_path_sizing import slack
from receiver_path_sizing import digest
from tool_process import configure_cleanup, execute


def run(args):
    with tempfile.TemporaryDirectory(prefix='receiver-unbuffer-') as temporary:
        work = Path(temporary)
        nets = ['q'] + [f'n{i}' for i in range(23)] + ['y']
        (work / 'fixture.v').write_text(
            'module PCSReceiver(input clk, input reset, output y);\nwire ' + ', '.join(nets[:-1]) + ';\n'
            'sg13g2_dfrbpq_1 state (.CLK(clk), .RESET_B(reset), .D(y), .Q(q));\n' +
            ''.join(f'sg13g2_buf_1 place{100 + i} (.A({a}), .X({b}));\n'
                    for i, (a, b) in enumerate(zip(nets, nets[1:]))) + 'endmodule\n')
        if args.buffer_sinks > 2:
            path = work / 'fixture.v'
            path.write_text(path.read_text().replace('output y);', 'output y, output extra0, output extra1);')
                .replace('endmodule', 'sg13g2_buf_1 other0 (.A(n13), .X(extra0));\n'
                         'sg13g2_buf_1 other1 (.A(n13), .X(extra1));\nendmodule'))
        if args.full_route:
            # Extra independent signal nets exercise the production SPEF
            # completeness guard, not a weakened fixture-only timing checker.
            path = work / 'fixture.v'
            path.write_text(path.read_text().replace('module PCSReceiver(',
                'module PCSReceiver(input [47:0] extra_input, output [47:0] extra_output, ')
                .replace('endmodule', ''.join(
                    f'sg13g2_buf_1 extra_buffer_{i} (.A(extra_input[{i}]), '
                    f'.X(extra_output[{i}]));\n' for i in range(48)) + 'endmodule'))
        (work / 'input.sdc').write_text('''create_clock -name sample_clock -period 3 [get_ports clk]
set_input_transition 0.1 [all_inputs]
set_case_analysis 1 [get_ports reset]
set_false_path -from [get_ports reset]
set_load 0.005 [get_ports y]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
''')
        if args.full_route:
            with (work / 'input.sdc').open('a') as sdc:
                sdc.write('set_input_delay 0.2 -clock sample_clock [get_ports {extra_input[*]}]\n'
                          'set_output_delay 0 -clock sample_clock [get_ports {extra_output[*]}]\n'
                          'set_load 0.005 [get_ports {extra_output[*]}]\n')
        if args.electrical_repair:
            sdc = work / 'input.sdc'
            sdc.write_text(sdc.read_text().replace('set_load 0.005', 'set_load 0.08'))
        models = work / 'cells.v'
        models.write_text(''.join(
            f'module sg13g2_buf_{s}(A, X); input A; output X; assign X=A; endmodule\n'
            for s in (1, 2, 4, 8, 16)) + ''.join(
            f'module sg13g2_dfrbpq_{s}(CLK, D, Q, RESET_B); input CLK, D, RESET_B; output Q; reg Q; '
            'always @(posedge CLK or negedge RESET_B) if (!RESET_B) Q<=0; else Q<=D; endmodule\n'
            for s in (1, 2)))
        libraries = '''set work $::env(REPEATER_WORK)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$lib/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
'''
        (work / 'fixture.tcl').write_text(libraries + '''read_lef "$lib/lef/sg13g2_tech.lef"
read_lef "$lib/lef/sg13g2_stdcell.lef"
read_verilog "$work/fixture.v"
link_design PCSReceiver
read_sdc "$work/input.sdc"
initialize_floorplan -die_area {0 0 200 200} -core_area {20 20 180 180} -site CoreSite
make_tracks
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.55
detailed_placement
write_db "$work/input.odb"
exit
''')
        (work / 'reload.tcl').write_text(libraries + '''read_db "$work/candidate.odb"
read_sdc "$work/candidate.sdc"
set_propagated_clock [all_clocks]
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
global_route
estimate_parasitics -placement
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/reload_setup.rpt"
report_worst_slack -min -digits 9 > "$work/reload_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/reload_electrical.rpt"
write_verilog "$work/reload.v"
exit
''')
        environment = dict(os.environ, REPEATER_WORK=str(work), REPEATER_PDK=str(args.pdk.resolve()),
                           REPEATER_SOURCE=str(HERE.parents[1] / 'pdk/1G_repeater_IHPSG13G2'),
                           RECEIVER_SOURCE=str(HERE), RECEIVER_SEED=str(work / 'input.odb'),
                           RECEIVER_SDC=str(work / 'input.sdc'), RECEIVER_UNBUFFER_SPAN='150',
                           RECEIVER_UNBUFFER_SINKS=str(args.buffer_sinks), RECEIVER_UNBUFFER_ROUNDS='4', RECEIVER_UNBUFFER_TARGET='0.35',
                           RECEIVER_UNBUFFER_REPAIR='1' if args.electrical_repair else '0')
        for name, script in [('fixture', work / 'fixture.tcl'),
                             ('unbuffer', HERE / 'receiver_global_unbuffer.tcl'),
                             ('reload', work / 'reload.tcl')]:
            with (work / (name + '.log')).open('w') as log:
                result = execute([args.openroad.resolve(), '-no_init', '-exit', script],
                                 cwd=work, env=environment, stdout=log, stderr=-2, timeout=90)
            if result.returncode:
                raise ValueError(name + ' failed:\n' + (work / (name + '.log')).read_text())
        proof = (compare_rebuffer(work / 'before.v', work / 'after.v', models) if args.electrical_repair
                 else compare(work / 'before.v', work / 'after.v'))
        if not proof['removed_buffers']:
            raise ValueError('fixture did not exercise buffer removal')
        if args.buffer_sinks > 2:
            removals = []
            for path in work.glob('removed_*.csv'):
                with path.open() as stream:
                    removals.extend(csv.DictReader(stream))
            if not any(int(row['sinks']) == 3 for row in removals):
                raise ValueError('fixture did not remove a three-sink buffer')
        if args.electrical_repair:
            if 'VIOLATED' not in (work / 'before_electrical.rpt').read_text():
                raise ValueError('fixture did not exercise an electrical violation')
            if not (proof['inserted_buffers'] or proof['replacements']):
                raise ValueError('fixture did not exercise electrical repair')
        compare(work / 'after.v', work / 'reload.v')
        if slack(work / 'after_setup.rpt', 'max') <= slack(work / 'before_setup.rpt', 'max'):
            raise ValueError('redundant buffer removal did not improve setup')
        for kind, mode in [('setup', 'max'), ('hold', 'min')]:
            value = slack(work / ('after_' + kind + '.rpt'), mode)
            if value < 0 or abs(value - slack(work / ('reload_' + kind + '.rpt'), mode)) > .001:
                raise ValueError(kind + ' fails or differs after independent route reload')
        for name in ('after', 'reload'):
            if 'VIOLATED' in (work / (name + '_electrical.rpt')).read_text():
                raise ValueError('fixture electrical limits fail')
        if args.route_probe or args.full_route:
            from receiver_route_probe import run as probe
            tools = args.openroad.resolve().parents[1]
            sources = [tools / 'bin/openroad', tools / 'root/usr/bin/openroad', models]
            sources += [args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib' /
                        f'sg13g2_stdcell_{corner}.lib' for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C')]
            receipt = dict(status='diagnostic_complete', cell_models=str(models), rounds=[dict(name='after',
                connectivity=proof, electrical_pass=True,
                setup_ns=slack(work / 'after_setup.rpt', 'max'), hold_ns=slack(work / 'after_hold.rpt', 'min'))],
                source_sha256={str(p): digest(p) for p in sources},
                artifact_sha256={str(work / name): digest(work / name)
                                 for name in ('candidate.odb', 'candidate.sdc', 'after.v')})
            (work / 'checks.json').write_text(json.dumps(receipt))
            output = work / 'probe'
            probe(SimpleNamespace(seed=work, output=output, pdk=args.pdk, tools=tools,
                                  minimum_free_mib=0, allocator=args.allocator))
            measured = json.loads((output / 'checks.json').read_text())
            if measured['status'] != 'early_route_diagnostic_complete' or measured['connectivity']['status'] != 'pass':
                raise ValueError('early route did not produce measured connectivity/timing evidence')
            if any(measured[name] for name in ('timing_qualified', 'physical_qualified', 'full_chip_qualified')):
                raise ValueError('early routing incorrectly claimed qualification')
            from receiver_routed_seed import prepare
            diagnostic = work / 'probe-seed'
            prepare(SimpleNamespace(route=output, output=diagnostic, constraints=work / 'candidate.sdc',
                cells=models, pdk=args.pdk, tools=tools, probe_origin=work))
            seed = json.loads((diagnostic / 'checks.json').read_text())
            if (seed['route_scope'] != 'early_route_diagnostic' or
                    seed['status'] != 'diagnostic_complete' or not seed['fresh_process_timing_agrees'] or
                    any(seed[key] for key in ('timing_qualified', 'physical_qualified', 'full_chip_qualified'))):
                raise ValueError('early-wire seed did not preserve diagnostic scope and fresh timing')
            if args.allocator:
                library = args.allocator.resolve()
                if (measured['allocator']['library'] != str(library) or
                        measured['source_sha256'].get(str(library)) != digest(library)):
                    raise ValueError('route allocator is not bound to the receipt')
            if args.full_route:
                from resume_receiver_route import run as resume
                routed = work / 'full-route'
                resume(SimpleNamespace(origin=work, constraints=work / 'candidate.sdc',
                    route_probe=output, output=routed, pdk=args.pdk, tools=tools, allocator=args.allocator))
                complete = json.loads((routed / 'checks.json').read_text())
                if (complete['status'] != 'routed_timing_candidate_pass' or
                        complete['routed_connectivity']['status'] != 'pass' or
                        complete['timing']['extracted_nets'] < 40):
                    raise ValueError('full-route handoff failed timing, extraction or endpoint checks')
                if complete['physical_qualified'] or complete['full_chip_qualified']:
                    raise ValueError('standalone routing incorrectly claimed layout/chip qualification')
        print('PASS exact endpoints, improved setup, hold/electrical limits, independent routed RC reload')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openroad', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--buffer-sinks', type=int, choices=(2, 4), default=2)
    parser.add_argument('--electrical-repair', action='store_true')
    parser.add_argument('--route-probe', action='store_true')
    parser.add_argument('--full-route', action='store_true')
    parser.add_argument('--allocator', type=Path)
    run(parser.parse_args())
