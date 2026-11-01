#!/usr/bin/env python3
"""Test clock-only NDR recovery against actual OpenDB rule assignments."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--openroad', type=Path, required=True)
args = parser.parse_args()
helper = Path(__file__).resolve().parents[1] / 'receiver_routing.tcl'
with tempfile.TemporaryDirectory(prefix='receiver-routing-') as temporary:
    work = Path(temporary)
    script = work / 'guard.tcl'
    script.write_text('''source $::env(CPPHDL_RECEIVER_NDR_SCRIPT)
set database [ord::get_db]
set tech [odb::dbTech_create $database test]
set chip [odb::dbChip_create $database $tech]
set block [odb::dbBlock_create $chip test]
set clock [odb::dbNet_create $block clock]
set data [odb::dbNet_create $block data]
set plain [odb::dbNet_create $block plain]
set rule [odb::dbTechNonDefaultRule_create $block CTS_NDR_0]
$clock setNonDefaultRule $rule
$data setNonDefaultRule $rule
set count [remove_receiver_clock_ndr [list $clock $plain] recovery.csv]
if {$count != 1 || [$clock getNonDefaultRule] != "NULL" || [$data getNonDefaultRule] != $rule} { error "wrong rule boundary" }
set custom [odb::dbTechNonDefaultRule_create $block custom_clock]
$clock setNonDefaultRule $custom
if {![catch {remove_receiver_clock_ndr [list $data $clock] rejected.csv} message]} { error "custom rule accepted" }
if {[$clock getNonDefaultRule] != $custom || [$data getNonDefaultRule] != $rule} { error "partial update on failed validation" }
$clock setNonDefaultRule $rule
$rule setHardSpacing 1
if {![catch {remove_receiver_clock_ndr [list $clock] rejected.csv} message]} { error "hard rule accepted" }
if {[$clock getNonDefaultRule] != $rule} { error "hard rule altered" }
puts "PASS clock-only recovery, default/data preservation, custom/hard rejection and atomic validation"
exit
''')
    with (work / 'openroad.log').open('w') as log:
        result = subprocess.run([args.openroad.resolve(), '-no_init', '-exit', script], cwd=work,
            env=dict(os.environ, CPPHDL_RECEIVER_NDR_SCRIPT=str(helper)), stdout=log, stderr=-2, timeout=60)
    output = (work / 'openroad.log').read_text()
    if result.returncode or 'PASS clock-only recovery' not in output:
        raise RuntimeError(output)
    if (work / 'recovery.csv').read_text() != 'net,previous_rule,new_rule\nclock,CTS_NDR_0,PDK_default\n':
        raise ValueError('incorrect recovery audit')
    if (work / 'rejected.csv').exists():
        raise ValueError('invalid rule produced a recovery receipt')
    print('PASS real OpenDB clock-only recovery and unchanged custom/hard/data rules')
