#!/usr/bin/env python3
"""Exercise sizing after a fresh OpenDB/SPEF reload, using a generated tiny circuit."""
import argparse
import csv
import os
from pathlib import Path
import subprocess
import tempfile
import sys

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from buffer_equivalence import parse, connectivity

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--openroad', type=Path, required=True)
parser.add_argument('--pdk', type=Path, required=True)
parser.add_argument('--tradeoffs', action='store_true')
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='receiver-routed-sizing-') as temporary:
    work = Path(temporary)
    fixture = work / 'fixture'
    fixture.mkdir()
    output = work / 'sizing'
    output.mkdir()
    (fixture / 'test.v').write_text('''module PCSReceiver(input clk, input a, output y);
wire n1, n2;
sg13g2_buf_1 first (.A(a), .X(n1));
sg13g2_buf_1 second (.A(n1), .X(n2));
sg13g2_buf_1 third (.A(n2), .X(y));
endmodule
''')
    (output / 'input.sdc').write_text('''create_clock -name sample_clock -period 0.3 [get_ports clk]
set_input_delay 0 -clock sample_clock [get_ports a]
set_input_transition 0.1 [all_inputs]
set_output_delay 0 -clock sample_clock [all_outputs]
set_load 0.08 [all_outputs]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
''')
    prepare = work / 'prepare.tcl'
    prepare.write_text('''set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set fixture $::env(RECEIVER_ROUTED_WORK)
set_thread_count 1
read_lef "$lib/lef/sg13g2_tech.lef"
read_lef "$lib/lef/sg13g2_stdcell.lef"
define_corners slow fast
read_liberty -corner slow "$lib/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_verilog "$fixture/test.v"
link_design PCSReceiver
read_sdc "$::env(REPEATER_WORK)/input.sdc"
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
    environment = dict(os.environ, REPEATER_PDK=str(args.pdk.resolve()),
                       RECEIVER_ROUTED_WORK=str(fixture), REPEATER_WORK=str(output), OMP_NUM_THREADS='1')
    sizing = HERE / 'receiver_extracted_sizing.tcl'
    if args.tradeoffs:
        sizing = work / 'tradeoffs.tcl'
        sizing.write_text('''set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib"
set work $::env(REPEATER_WORK)
set fixture $::env(RECEIVER_ROUTED_WORK)
define_corners slow fast
read_liberty -corner slow "$lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$fixture/core.odb"
read_sdc "$work/input.sdc"
write_guides "$work/restored.guide"
read_guides "$work/restored.guide"
foreach corner {slow fast} { read_spef -corner $corner "$fixture/core.spef" }
write_verilog "$work/before.v"
source "$::env(RECEIVER_SOURCE)/receiver_timing_tradeoffs.tcl"
receiver_timing_tradeoffs {{third sg13g2_buf_16} {third sg13g2_buf_1}} \\
    "$work/setup.csv" 0.1
# This infeasible guard must reject a change even if it improves setup.
receiver_timing_tradeoffs {{first sg13g2_buf_16}} \\
    "$work/hold.csv" 10.0
write_verilog "$work/after.v"
exit
''')
        environment['RECEIVER_SOURCE'] = str(HERE)
    for name, script in [('prepare', prepare), ('reload-and-size', sizing)]:
        result = subprocess.run(['prlimit', '--as=1073741824', '--core=0', '--',
                                 args.openroad.resolve(), '-no_init', '-exit', script], cwd=work,
                                env=environment, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(name + ' failed:\n' + result.stdout + result.stderr)
    before, after = parse(output / 'before.v'), parse(output / 'after.v')
    if before['interface'] != after['interface'] or set(before['cells']) != set(after['cells']):
        raise ValueError('fixture interface or instance inventory changed')
    resized = 0
    for name, (old, pins) in before['cells'].items():
        new, new_pins = after['cells'][name]
        if old != new:
            resized += 1
        if new not in {'sg13g2_buf_' + str(size) for size in (1, 2, 4, 8, 16)}:
            raise ValueError('unexpected fixture cell replacement')
        after['cells'][name] = (old, new_pins)
    if not resized or connectivity(before, set()) != connectivity(after, set()):
        raise ValueError('no sizing or changed fixture signal connectivity')
    if args.tradeoffs:
        with (output / 'setup.csv').open() as stream:
            decisions = list(csv.DictReader(stream))
        with (output / 'hold.csv').open() as stream:
            hold = list(csv.DictReader(stream))
        if [r['accepted'] for r in decisions] != ['1', '0'] or hold[0]['accepted'] != '0':
            raise ValueError('setup or hold guard failed: ' + str((decisions, hold)))
        if parse(output / 'after.v')['cells']['first'][0] != 'sg13g2_buf_1':
            raise ValueError('rejected hold trial did not restore the original master')
    print('PASS fresh OpenDB/SPEF reload, restored guide state, resizing and unchanged signal endpoints')
