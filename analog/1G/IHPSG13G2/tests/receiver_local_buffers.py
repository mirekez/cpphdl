#!/usr/bin/env python3
"""Exercise local unbuffering boundaries on real OpenDB connectivity."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--openroad', type=Path, required=True)
args = parser.parse_args()
helper = Path(__file__).resolve().parents[1] / 'receiver_local_unbuffer.tcl'
with tempfile.TemporaryDirectory(prefix='receiver-local-buffer-') as temporary:
    work = Path(temporary)
    (work / 'units.lef').write_text('VERSION 5.8 ;\nUNITS\n DATABASE MICRONS 1000 ;\nEND UNITS\nEND LIBRARY\n')
    script = work / 'guards.tcl'
    script.write_text('''source $::env(CPPHDL_BUFFER_HELPER)
read_lef units.lef
set database [ord::get_db]
set tech [$database getTech]
set library [odb::dbLib_create $database test $tech]
set chip [odb::dbChip_create $database $tech]
set block [odb::dbBlock_create $chip PCSReceiver]
$block setDefUnits 1000
foreach name {sg13g2_buf_1 sg13g2_nand2_1 sg13g2_dfrbpq_1} {
    set master [odb::dbMaster_create $library $name]
    $master setWidth 1000
    $master setHeight 2000
    odb::dbMTerm_create $master A INPUT SIGNAL
    odb::dbMTerm_create $master X OUTPUT SIGNAL
    $master setFrozen
}
foreach {name master x} {head sg13g2_nand2_1 0 place42 sg13g2_buf_1 5000 tail sg13g2_nand2_1 10000 extra sg13g2_nand2_1 12000 third sg13g2_nand2_1 15000 fourth sg13g2_nand2_1 18000} {
    set instance [odb::dbInst_create $block [$library findMaster $master] $name]
    $instance setLocation $x 0
    $instance setPlacementStatus PLACED
    set $name $instance
}
set input [odb::dbNet_create $block input]
set output [odb::dbNet_create $block output]
[$head findITerm X] connect $input
[$place42 findITerm A] connect $input
[$place42 findITerm X] connect $output
[$tail findITerm A] connect $output
rename sta::find_all_clk_nets sta::find_all_clk_nets_original
set test_clocks {}
proc sta::find_all_clk_nets {} { return $::test_clocks }
proc count_candidates {} { return [llength [receiver_local_buffer_candidates {place42} 20]] }
if {[count_candidates] != 1} { error "short logic-driven buffer was not selected" }
set test_clocks [list $input]
if {[count_candidates]} { error "clock buffer selected" }
set test_clocks {}
$place42 setDoNotTouch 1
if {[count_candidates]} { error "protected buffer selected" }
$place42 setDoNotTouch 0
$head setDoNotTouch 1
if {[count_candidates]} { error "protected driver selected" }
$head setDoNotTouch 0
$output setDoNotTouch 1
if {[count_candidates]} { error "protected output selected" }
$output setDoNotTouch 0
$tail setPlacementStatus LOCKED
if {[count_candidates]} { error "fixed neighbor selected" }
$tail setPlacementStatus PLACED
$tail setLocation 40000 0
if {[count_candidates]} { error "distant sink selected" }
$tail setLocation 10000 0
if {[llength [receiver_local_buffer_candidates {place42} 150 200]] != 1} {
    error "explicit diagnostic span rejected"
}
$tail setLocation 140000 0
if {[count_candidates]} { error "default span accepted a distant sink" }
if {[llength [receiver_local_buffer_candidates {place42} 150 200]] != 1} {
    error "bounded extended diagnostic span rejected"
}
$tail setLocation 210000 0
if {[llength [receiver_local_buffer_candidates {place42} 200 200]]} {
    error "extended diagnostic span accepted an out-of-bound sink"
}
$tail setLocation 10000 0
foreach limit {0 -1 201 NaN Inf} {
    if {![catch {receiver_local_buffer_candidates {place42} 60 $limit}]} {
        error "invalid diagnostic span limit accepted"
    }
}
set port [odb::dbBTerm_create $output external]
if {[count_candidates]} { error "port net selected" }
odb::dbBTerm_destroy $port
[$extra findITerm A] connect $input
if {[count_candidates]} { error "branched input selected" }
[$extra findITerm A] connect $output
if {[count_candidates] != 1} { error "two-sink local buffer rejected" }
[$third findITerm A] connect $output
if {[count_candidates]} { error "more than two sinks selected by default" }
if {[llength [receiver_local_buffer_candidates {place42} 20 60 3]] != 1} {
    error "explicit three-sink diagnostic rejected"
}
if {[llength [receiver_local_buffer_candidates {place42} 20 60 2]]} {
    error "explicit sink bound ignored"
}
foreach sinks {0 -1 5 2.5 NaN Inf} {
    if {![catch {receiver_local_buffer_candidates {place42} 20 60 $sinks}]} {
        error "invalid sink bound accepted"
    }
}
[$fourth findITerm A] connect $output
if {[llength [receiver_local_buffer_candidates {place42} 20 60 4]] != 1} {
    error "explicit four-sink diagnostic rejected"
}
if {[llength [receiver_local_buffer_candidates {place42} 20 60 3]]} {
    error "three-sink bound accepted four sinks"
}
[$fourth findITerm A] disconnect
[$third findITerm A] disconnect
$head swapMaster [$library findMaster sg13g2_dfrbpq_1]
if {[count_candidates]} { error "register loading changed" }
$head swapMaster [$library findMaster sg13g2_nand2_1]
if {[count_candidates] != 1} { error "candidate did not recover" }
foreach span {0 -1 61 NaN Inf} {
    if {![catch {receiver_local_buffer_candidates {place42} $span}]} { error "invalid span accepted" }
}
foreach names {{place42 place42} {hold42} {place999}} {
    if {![catch {receiver_local_buffer_candidates $names 20}]} { error "invalid explicit names accepted" }
}
puts "PASS local buffer selection and topology/protection/clock/span guards"
exit
''')
    result = subprocess.run([args.openroad.resolve(), '-no_init', '-exit', script], cwd=work,
        env=dict(os.environ, CPPHDL_BUFFER_HELPER=str(helper)), capture_output=True, text=True, timeout=60)
    if result.returncode or 'PASS local buffer selection' not in result.stdout:
        raise RuntimeError(result.stdout + result.stderr)
    print('PASS local buffer selection using real OpenDB connectivity')
