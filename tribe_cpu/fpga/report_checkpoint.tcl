# Report a saved implementation checkpoint without repeating synthesis/place.
# Usage: vivado -mode batch -source report_checkpoint.tcl -tclargs in.dcp out.rpt
if {$argc != 2} {
    error "usage: report_checkpoint.tcl <checkpoint.dcp> <report.rpt>"
}
set checkpoint [file normalize [lindex $argv 0]]
set report [file normalize [lindex $argv 1]]
file mkdir [file dirname $report]
open_checkpoint $checkpoint
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins -file $report
report_timing_summary -delay_type min_max -max_paths 100 -report_unconstrained \
    -file "${report}.summary"
