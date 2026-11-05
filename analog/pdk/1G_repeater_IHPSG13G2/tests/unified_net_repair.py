#!/usr/bin/env python3
"""Repair a heavily loaded IHP net, legalize it, and prove logic equivalence."""
import argparse
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tool_process import configure_cleanup, execute


def run(args):
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    liberty = library / 'lib/sg13g2_stdcell_slow_1p08V_125C.lib'
    with tempfile.TemporaryDirectory(prefix='repeater-net-repair-') as temporary:
        work = Path(temporary)
        design = ['module NetRepair(input A, B, output [127:0] Y);', 'wire loaded;',
                  'sg13g2_nor2_1 driver(.A(A), .B(B), .Y(loaded));']
        design += [f'sg13g2_buf_16 sink{i}(.A(loaded), .X(Y[{i}]));' for i in range(128)]
        (work / 'original.v').write_text('\n'.join(design + ['endmodule']) + '\n')
        (work / 'repair.tcl').write_text(f'''set_thread_count 1
read_lef {{{library}/lef/sg13g2_tech.lef}}
read_lef {{{library}/lef/sg13g2_stdcell.lef}}
read_liberty {{{liberty}}}
read_verilog {{{work}/original.v}}
link_design NetRepair
initialize_floorplan -die_area {{0 0 220 220}} -core_area {{10 10 210 210}} -site CoreSite
make_tracks
set_wire_rc -signal -layer Metal3
set_input_transition 0.1 [all_inputs]
set_load 0.005 [all_outputs]
set_max_fanout 8 [current_design]
set_max_capacitance 0.08 [current_design]
set_max_transition 0.3 [current_design]
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.3
detailed_placement
estimate_parasitics -placement
set block [ord::get_db_block]
if {{[[$block findNet loaded] getITermCount] != 129}} {{error "lost test load"}}
rsz::repair_net_cmd [sta::find_net loaded] 0 10 10
if {{[[$block findNet loaded] getITermCount] > 9}} {{error "fanout was not repaired"}}
detailed_placement
check_placement -verbose
estimate_parasitics -placement
write_verilog {{{work}/repaired.v}}
puts "PASS: bounded repair reduced fanout and retained legal placement"
''')
        script = []
        for kind, netlist in [('gold', 'original'), ('gate', 'repaired')]:
            script += [f'read_liberty -ignore_miss_func {liberty}',
                       f'read_verilog {work}/{netlist}.v', 'hierarchy -top NetRepair',
                       'proc', 'flatten', 'opt', f'rename NetRepair {kind}',
                       f'design -stash {kind}']
        script += ['design -copy-from gold -as gold gold', 'design -copy-from gate -as gate gate',
                   'equiv_make gold gate equiv', 'hierarchy -top equiv', 'equiv_simple', 'equiv_status -assert']
        (work / 'equivalence.ys').write_text('\n'.join(script) + '\n')
        for name, tool, arguments in [
                ('repair', 'openroad', ['-no_init', '-exit', work / 'repair.tcl']),
                ('equivalence', 'yosys', ['-s', work / 'equivalence.ys'])]:
            with (work / (name + '.log')).open('w') as log:
                result = execute([str(args.tools.resolve() / 'bin' / tool), *map(str, arguments)],
                                 cwd=work, stdout=log, stderr=-2, timeout=60)
            output = (work / (name + '.log')).read_text()
            if result.returncode or (name == 'repair' and 'PASS:' not in output):
                raise ValueError(output)
        print('PASS: 128-load IHP repair, legal placement and Yosys equivalence for every input')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    run(parser.parse_args())
