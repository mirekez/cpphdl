#!/usr/bin/env python3
"""Check local protection connectivity, fixed placement and idempotence in OpenDB."""
import argparse
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1]


def check(args):
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    with tempfile.TemporaryDirectory(prefix='cpphdl-input-antenna-') as temporary:
        work = Path(temporary)
        (work / 'design.v').write_text('''module Protection(input [1:0] comparisons_in,
    input unrelated, output [2:0] Y);
sg13g2_dlygate4sd3_1 D0(.A(comparisons_in[0]), .X(Y[0]));
sg13g2_dlygate4sd3_1 D1(.A(comparisons_in[1]), .X(Y[1]));
sg13g2_buf_1 B0(.A(unrelated), .X(Y[2]));
endmodule
''')
        script = f'''read_lef {{{library / 'lef/sg13g2_tech.lef'}}}
read_lef {{{library / 'lef/sg13g2_stdcell.lef'}}}
read_liberty {{{library / 'lib/sg13g2_stdcell_typ_1p20V_25C.lib'}}}
read_verilog {{{work / 'design.v'}}}
link_design Protection
initialize_floorplan -die_area {{0 0 60 60}} -core_area {{0 0 60 60}} -site CoreSite
add_global_connection -net VDD -inst_pattern .* -pin_pattern VDD -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern VSS -ground
global_connect
set block [ord::get_db_block]
set originals {{}}
foreach {{name x y}} {{D0 9600 7560 D1 30240 7560 B0 9600 22680}} {{
    set instance [$block findInst $name]
    $instance setOrient R0
    $instance setLocation $x $y
    $instance setPlacementStatus PLACED
    lappend originals [list $instance [$instance getLocation] [$instance getOrient]]
}}
source {{{SOURCE / 'antenna_inputs.tcl'}}}
if {{[protect_input_delay_gates {{comparisons_in*}} {{{work / 'first.csv'}}}] != 2}} {{
    error "comparison receivers were not protected"
}}
if {{[protect_input_delay_gates {{comparisons_in*}} {{{work / 'second.csv'}}}] != 2}} {{
    error "second protection pass changed the receiver count"
}}
global_connect
set count 0
foreach instance [$block getInsts] {{
    if {{[[$instance getMaster] getName] != "sg13g2_antennanp"}} {{continue}}
    incr count
    set net [[[$instance findITerm A] getNet] getName]
    if {{![string match {{comparisons_in*}} $net]}} {{error "diode on wrong signal"}}
    foreach rail {{VDD VSS}} {{
        if {{[[[$instance findITerm $rail] getNet] getName] != $rail}} {{error "diode rail disconnected"}}
    }}
}}
if {{$count != 2}} {{error "repeated protection inserted duplicate diodes"}}
foreach original $originals {{
    lassign $original instance location orientation
    if {{[$instance getLocation] != $location || [$instance getOrient] != $orientation ||
         [$instance getPlacementStatus] != "PLACED"}} {{error "original placement changed"}}
}}
check_placement -verbose
puts "PASS: local diodes, signal and rail connectivity, placement preservation and idempotence"
exit
'''
        (work / 'test.tcl').write_text(script)
        result = subprocess.run([str(args.tools.resolve() / 'bin/openroad'), '-no_init', '-exit',
                                 str(work / 'test.tcl')], capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        if result.returncode or '[ERROR' in output or 'PASS:' not in output:
            raise ValueError(output)
        print(output[output.index('PASS:'):].strip())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    check(parser.parse_args())
