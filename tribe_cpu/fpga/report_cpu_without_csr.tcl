set script_dir [file dirname [file normalize [info script]]]
set run_dir [file normalize [expr {
    [info exists ::env(TRIBE_RUN_DIR)] && $::env(TRIBE_RUN_DIR) ne "" ?
    $::env(TRIBE_RUN_DIR) : [file join $script_dir vivado_cpu_fetch_stage_raw]
}]]
open_checkpoint [file join $run_dir checkpoints placed_raw.dcp]

# Diagnostic only: hide CSR sequential endpoints to expose the next CPU cone
# from the same placement.  This does not alter implementation constraints.
set csr_seq [get_cells -hier -quiet -filter {NAME =~ csr/* && IS_SEQUENTIAL}]
set_false_path -to $csr_seq
report_timing -delay_type max -max_paths 40 -nworst 10 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks cpu_clk] \
    -file [file join $run_dir reports timing_cpu_without_csr.rpt]
exit
