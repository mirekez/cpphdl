"""Exercise the actual Tcl acceptance rule without an EDA installation."""
from pathlib import Path
import subprocess
import shutil
import sys

if not shutil.which('tclsh'):
    print('SKIP: tclsh is required to exercise the production Tcl predicate')
    sys.exit(77)

source = Path(__file__).resolve().parents[1] / 'receiver_checkpoint_choice.tcl'
script = '''
set seed [dict create nominal_setup 0.3 nominal_hold 0.12 margin_setup -0.8 \
    margin_hold 0.14 margin_tns -20 nominal_electrical 1 margin_electrical 1]
set good [dict replace $seed nominal_setup 0.4 margin_setup -0.5 margin_tns -15]
if {![receiver_checkpoint_improves $seed $good]} {error "improvement rejected"}
if {[receiver_checkpoint_improves $seed $seed]} {error "unchanged candidate accepted"}
foreach {key value} {nominal_setup 0.2 nominal_hold 0.09 margin_hold 0.09 \
    margin_setup -0.9 margin_tns -21 nominal_electrical 0 margin_electrical 0} {
    if {[receiver_checkpoint_improves $seed [dict replace $good $key $value]]} {
        error "regression accepted: $key"
    }
}
foreach value {NaN Inf -Inf invalid} {
    if {![catch {receiver_checkpoint_improves $seed [dict replace $good margin_setup $value]}]} {
        error "nonfinite metric accepted: $value"
    }
}
if {![catch {receiver_checkpoint_improves $seed [dict remove $good nominal_hold]}]} {
    error "incomplete candidate accepted"
}
puts "PASS checkpoint selection: improvement, regressions, unchanged and invalid metrics"
'''
subprocess.run(['tclsh'], input='source {' + str(source) + '}\n' +
               'if {[catch {\n' + script + '\n} message]} {puts stderr $message; exit 1}\n',
               text=True, check=True)
