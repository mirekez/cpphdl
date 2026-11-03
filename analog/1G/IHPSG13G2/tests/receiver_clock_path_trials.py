#!/usr/bin/env python3
"""Check bounded clock-driver selection on actual OpenDB objects."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--openroad', type=Path, required=True)
args = parser.parse_args()
helper = Path(__file__).resolve().parents[1] / 'receiver_clock_path_trials.tcl'
with tempfile.TemporaryDirectory(prefix='receiver-clock-trials-') as temporary:
    work = Path(temporary)
    (work / 'units.lef').write_text('VERSION 5.8 ;\nUNITS\n DATABASE MICRONS 1000 ;\nEND UNITS\nEND LIBRARY\n')
    script = work / 'guards.tcl'
    script.write_text('''source $::env(CPPHDL_CLOCK_HELPER)
read_lef units.lef
set db [ord::get_db]
set tech [$db getTech]
set lib [odb::dbLib_create $db test $tech]
set chip [odb::dbChip_create $db $tech]
set block [odb::dbBlock_create $chip PCSReceiver]
foreach name {sg13g2_buf_8 sg13g2_dfrbpq_1} {
    set master [odb::dbMaster_create $lib $name]
    $master setWidth 1000
    $master setHeight 2000
    odb::dbMTerm_create $master A INPUT SIGNAL
    odb::dbMTerm_create $master CLK INPUT CLOCK
    odb::dbMTerm_create $master X OUTPUT SIGNAL
    $master setFrozen
}
foreach {name type} {root sg13g2_buf_8 leaf sg13g2_buf_8 reg sg13g2_dfrbpq_1} {
    set inst [odb::dbInst_create $block [$lib findMaster $type] $name]
    $inst setPlacementStatus PLACED
    set $name $inst
}
set input [odb::dbNet_create $block input]
set branch [odb::dbNet_create $block branch]
set clock [odb::dbNet_create $block clock]
[$root findITerm A] connect $input
[$root findITerm X] connect $branch
[$leaf findITerm A] connect $branch
[$leaf findITerm X] connect $clock
[$reg findITerm CLK] connect $clock
proc report {text} { set f [open paths.rpt w]; puts $f $text; close $f }
report "Startpoint: reg (clocked register)\\nEndpoint: reg (clocked register)"
set trials [receiver_clock_path_trials paths.rpt]
if {[llength $trials] != 8 || [llength [lsort -unique $trials]] != 8} { error "clock trials not bounded and deduplicated" }
if {[lsearch -exact $trials {leaf sg13g2_buf_1}] < 0} { error "missing leaf drive trial" }
if {[lsearch -exact $trials {root sg13g2_buf_16}] < 0} { error "missing parent drive trial" }
$root setDoNotTouch 1
if {[llength [receiver_clock_path_trials paths.rpt]] != 4} { error "protected parent selected" }
$root setDoNotTouch 0
$leaf setDoNotTouch 1
if {[llength [receiver_clock_path_trials paths.rpt]] != 0} { error "protected leaf selected" }
$leaf setDoNotTouch 0
$leaf setPlacementStatus LOCKED
if {[llength [receiver_clock_path_trials paths.rpt]] != 0} { error "fixed clock driver selected" }
$leaf setPlacementStatus PLACED
$clock setDoNotTouch 1
if {[llength [receiver_clock_path_trials paths.rpt]] != 0} { error "protected clock net selected" }
$clock setDoNotTouch 0
report "Endpoint: missing (register)"
if {![catch {receiver_clock_path_trials paths.rpt}]} { error "missing endpoint accepted" }
report "no paths"
if {![catch {receiver_clock_path_trials paths.rpt}]} { error "empty report accepted" }
puts "PASS clock selection and protection guards"
exit
''')
    result = subprocess.run([args.openroad.resolve(), '-no_init', '-exit', script], cwd=work,
        env=dict(os.environ, CPPHDL_CLOCK_HELPER=str(helper)), capture_output=True, text=True, timeout=60)
    if result.returncode or 'PASS clock selection' not in result.stdout:
        raise RuntimeError(result.stdout + result.stderr)
    print('PASS clock-path selection on real OpenDB objects')
