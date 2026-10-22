#!/usr/bin/env python3
"""Exercise cleanup with the real OpenDB: connected and wired nets must survive."""
import argparse
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1]


def check(args):
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    with tempfile.TemporaryDirectory(prefix='cpphdl-constant-nets-') as temporary:
        work = Path(temporary)
        (work / 'design.v').write_text('module NetCleanup(input A, output Y);\n'
                                     'sg13g2_inv_1 INV(.A(A), .Y(Y));\nendmodule\n')
        script = f'''read_lef {{{library / 'lef/sg13g2_tech.lef'}}}
read_lef {{{library / 'lef/sg13g2_stdcell.lef'}}}
read_liberty {{{library / 'lib/sg13g2_stdcell_typ_1p20V_25C.lib'}}}
read_verilog {{{work / 'design.v'}}}
link_design NetCleanup
source {{{SOURCE / 'clean_empty_nets.tcl'}}}
set block [ord::get_db_block]
set zero [odb::dbNet_create $block zero_]
$zero setSigType GROUND
set one [odb::dbNet_create $block one_]
$one setSigType POWER
set terminal [odb::dbBTerm_create $one GUARD]
set scratch [odb::dbNet_create $block scratch]
if {{[clean_empty_constant_nets] != 1}} {{error "empty reserved ground was not removed"}}
if {{[$block findNet one_] == "NULL" || [$block findNet scratch] == "NULL"}} {{error "protected net was removed"}}
if {{[llength [$one getBTerms]] != 1}} {{error "external supply terminal was changed"}}
odb::dbBTerm_destroy $terminal
set wire [odb::dbSWire_create $one ROUTED]
if {{[clean_empty_constant_nets] != 0}} {{error "physical supply wire was removed"}}
odb::dbSWire_destroy $wire
set iterm [[$block findInst INV] findITerm A]
$iterm disconnect
$iterm connect $one
if {{[clean_empty_constant_nets] != 0}} {{error "connected supply net was removed"}}
if {{[$iterm getNet] != $one}} {{error "gate connectivity was changed"}}
puts "PASS: empty constant removed; external, wired, connected and ordinary nets preserved"
exit
'''
        (work / 'test.tcl').write_text(script)
        result = subprocess.run([str(args.tools.resolve() / 'bin/openroad'), '-no_init', '-exit',
                                 str(work / 'test.tcl')], capture_output=True, text=True, timeout=30)
        text = result.stdout + result.stderr
        if result.returncode or '[ERROR' in text or 'PASS:' not in text:
            raise ValueError(text)
        print(text[text.index('PASS:'):].strip())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    check(parser.parse_args())
