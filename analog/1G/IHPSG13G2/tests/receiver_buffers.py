#!/usr/bin/env python3
"""Exercise buffer selection boundaries on real OpenDB objects."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--openroad', type=Path, required=True)
args = parser.parse_args()
helper = Path(__file__).resolve().parents[1] / 'receiver_buffers.tcl'
with tempfile.TemporaryDirectory(prefix='receiver-buffer-guards-') as temporary:
    work = Path(temporary)
    (work / 'units.lef').write_text('VERSION 5.8 ;\nUNITS\n  DATABASE MICRONS 1000 ;\nEND UNITS\nEND LIBRARY\n')
    script = work / 'guards.tcl'
    script.write_text('''source $::env(CPPHDL_BUFFER_HELPER)
set database [ord::get_db]
read_lef units.lef
set tech [$database getTech]
set library [odb::dbLib_create $database test $tech]
set chip [odb::dbChip_create $database $tech]
set block [odb::dbBlock_create $chip PCSReceiver]
$block setDefUnits 1000
set master [odb::dbMaster_create $library sg13g2_buf_2]
$master setWidth 4000
$master setHeight 2000
odb::dbMTerm_create $master A INPUT SIGNAL
odb::dbMTerm_create $master X OUTPUT SIGNAL
$master setFrozen
foreach {name x} {head 0 wire42 5000 tail 10000} {
    set $name [odb::dbInst_create $block $master $name]
    [set $name] setLocation $x 0
    [set $name] setPlacementStatus PLACED
}
set input [odb::dbNet_create $block input]
set output [odb::dbNet_create $block output]
[$head findITerm X] connect $input
[$wire42 findITerm A] connect $input
[$wire42 findITerm X] connect $output
[$tail findITerm A] connect $output
proc count_candidates {} {
    global block
    return [llength [receiver_series_buffer_candidates $block {} 20]]
}
if {[count_candidates] != 1} {error "short series buffer not selected"}
if {[llength [receiver_series_buffer_candidates $block [list $input] 20]]} {error "clock buffer selected"}
$wire42 setDoNotTouch 1
if {[count_candidates]} {error "protected buffer selected"}
$wire42 setDoNotTouch 0
$head setDoNotTouch 1
if {[count_candidates]} {error "protected driver selected"}
$head setDoNotTouch 0
$output setDoNotTouch 1
if {[count_candidates]} {error "protected net selected"}
$output setDoNotTouch 0
$tail setPlacementStatus LOCKED
if {[count_candidates]} {error "fixed neighbor selected"}
$tail setPlacementStatus PLACED
$tail setLocation 40000 0
if {[count_candidates]} {error "long chain selected"}
$tail setLocation 10000 0
set port [odb::dbBTerm_create $output external]
if {[count_candidates]} {error "external port buffer selected"}
odb::dbBTerm_destroy $port
set extra [odb::dbInst_create $block $master extra]
[$extra findITerm A] connect $input
if {[count_candidates]} {error "branched chain selected"}
[$extra findITerm A] disconnect
if {[count_candidates] != 1} {error "candidate did not recover after guards"}
foreach span {0 -1 21 NaN Inf} {
    if {![catch {receiver_series_buffer_candidates $block {} $span} message]} {error "invalid span accepted"}
}
puts "PASS short-chain selection and clock/protection/fixed/port/fanout/span guards"
exit
''')
    result = subprocess.run([args.openroad.resolve(), '-no_init', '-exit', script], cwd=work,
        env=dict(os.environ, CPPHDL_BUFFER_HELPER=str(helper)), stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, timeout=60)
    if result.returncode or 'PASS short-chain selection' not in result.stdout:
        raise RuntimeError(result.stdout)
    print('PASS real OpenDB short-chain selection and protection guards')
