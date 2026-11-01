#!/usr/bin/env python3
"""Exercise selective clock-tree edits with real IHP cells in OpenROAD."""
import argparse
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1]


def check(args):
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    with tempfile.TemporaryDirectory(prefix='cpphdl-clock-tree-') as temporary:
        work = Path(temporary)
        for case in ('valid', 'wrong_top', 'wrong_driver', 'empty_selection', 'branch_collision'):
            top = 'Other' if case == 'wrong_top' else 'ProcessingClockControl'
            design = [f'module {top}(input pcs_clk, ref_clk, D, RESET_B, output Y, Z);',
                      'wire qualified_ready;',
                      'sg13g2_buf_4 data_buffer(.A(D), .X(Z));']
            if case == 'branch_collision':
                design += ['wire collision;', 'sg13g2_buf_4 clkbuf_readiness(.A(D), .X(collision));']
            for clock in ('pcs_clk', 'ref_clk'):
                previous = clock
                for level in range(6):
                    net = f'{clock}_{level}'
                    name = f'clkbuf_{level}_{clock}'
                    if case == 'empty_selection' and level in (1, 3):
                        name = f'unselected_{level}_{clock}'
                    design += [f'wire {net};',
                               f'sg13g2_buf_4 {name}(.A({previous}), .X({net}));']
                    previous = net
            if case == 'wrong_driver':
                design += ['sg13g2_buf_4 readiness(.A(D), .X(qualified_ready));']
            else:
                design += ['sg13g2_dfrbpq_1 readiness(.CLK(pcs_clk_5), .D(D), '
                           '.RESET_B(RESET_B), .Q(qualified_ready));']
            design += ['sg13g2_dfrbpq_1 reference(.CLK(ref_clk_5), .D(D), '
                       '.RESET_B(RESET_B), .Q(Y));', 'endmodule']
            (work / 'design.v').write_text('\n'.join(design) + '\n')
            checks = '''
if {$failed} {error "valid repair failed: $message"}
if {[llength [get_cells *]] != $before - 4 + 1} {error "wrong clock-tree cell count"}
foreach clock {pcs_clk ref_clk} {
    foreach level {1 3} {
        if {[llength [get_cells -quiet clkbuf_${level}_$clock]]} {error "buffer survived"}
    }
    foreach {level size} {0 16 2 8 4 4 5 4} {
        if {[get_property [get_cells clkbuf_${level}_$clock] ref_name] != "sg13g2_buf_$size"} {
            error "wrong retained buffer size"
        }
    }
}
set block [ord::get_db_block]
set ready_clock [[[$block findInst readiness] findITerm CLK] getNet]
set root_net [[[$block findInst clkbuf_readiness] findITerm X] getNet]
if {$ready_clock != $root_net} {error "readiness did not get the early branch"}
set branch_input [[[$block findInst clkbuf_readiness] findITerm A] getNet]
if {$branch_input != [[$block findBTerm pcs_clk] getNet]} {error "local branch has the wrong source"}
if {[get_property [get_cells clkbuf_readiness] ref_name] != "sg13g2_buf_8"} {
    error "wrong local branch driver"
}
if {[get_property [get_cells data_buffer] ref_name] != "sg13g2_buf_4"} {
    error "data buffer changed"
}
if {[get_name [get_nets -of_objects [get_pins data_buffer/A]]] != "D"} {
    error "data buffer was reconnected"
}
''' if case == 'valid' else '''
if {!$failed} {error "invalid tree accepted"}
if {[llength [get_cells *]] != $before} {error "rejection changed the design"}
foreach clock {pcs_clk ref_clk} {
    if {[get_property [get_cells clkbuf_0_$clock] ref_name] != "sg13g2_buf_4"} {
        error "rejection changed the root"
    }
}
'''
            script = f'''set_thread_count 1
read_lef {{{library}/lef/sg13g2_tech.lef}}
read_lef {{{library}/lef/sg13g2_stdcell.lef}}
read_liberty {{{library}/lib/sg13g2_stdcell_typ_1p20V_25C.lib}}
read_verilog {{{work}/design.v}}
link_design {top}
source {{{SOURCE}/processing_clock_tree.tcl}}
set before [llength [get_cells *]]
set failed [catch {{repair_processing_clock_tree}} message]
{checks}
puts "PASS: {case}"
exit
'''
            (work / 'test.tcl').write_text(script)
            result = subprocess.run(['/usr/bin/prlimit', '--as=1073741824', '--',
                                     str(args.tools.resolve() / 'bin/openroad'),
                                     '-no_init', '-exit', str(work / 'test.tcl')],
                                    capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            if result.returncode or '[ERROR' in output or f'PASS: {case}' not in output:
                raise ValueError(output)
            print(f'PASS: {case}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    check(parser.parse_args())
