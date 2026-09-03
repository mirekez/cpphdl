# Validate the target clocks and CDC exceptions against the synthesized cluster.

proc env_or_default {name fallback} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return $::env($name)
    }
    return $fallback
}

set script_dir [file dirname [file normalize [info script]]]
set synth_dcp [file normalize [env_or_default TRIBE_SYNTH_DCP \
    [file join $script_dir vivado_tribe_timing checkpoints post_synth.dcp]]]
set run_dir [file normalize [env_or_default TRIBE_RUN_DIR \
    [file join $script_dir vivado_constraint_check]]]
file mkdir $run_dir

open_checkpoint $synth_dcp
reset_timing
read_xdc [file join $script_dir tribe_312_156_constraints.xdc]

set first_stages [get_cells -hierarchical -quiet -filter \
    {ASYNC_REG == TRUE && NAME =~ *1_reg*}]
puts "TRIBE_CDC_FIRST_STAGE_COUNT=[llength $first_stages]"
puts "TRIBE_CPU_PERIOD=[get_property PERIOD [get_clocks cpu_clk]]"
puts "TRIBE_L2_PERIOD=[get_property PERIOD [get_clocks l2_clk]]"

report_clock_interaction -file [file join $run_dir clock_interaction.rpt]
report_exceptions -coverage -file [file join $run_dir exceptions.rpt]
report_drc -file [file join $run_dir drc.rpt]
check_timing -verbose -loop_limit 1000 \
    -file [file join $run_dir check_timing_verbose.rpt]
report_timing -delay_type max -max_paths 50 -nworst 10 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks cpu_clk] \
    -file [file join $run_dir cpu_to_cpu.rpt]
report_timing -delay_type max -max_paths 50 -nworst 10 -unique_pins \
    -from [get_clocks l2_clk] -to [get_clocks l2_clk] \
    -file [file join $run_dir l2_to_l2.rpt]
report_timing -delay_type max -max_paths 20 -nworst 5 \
    -from [get_clocks cpu_clk] -to [get_clocks l2_clk] \
    -file [file join $run_dir cpu_to_l2.rpt]
report_timing -delay_type max -max_paths 20 -nworst 5 \
    -from [get_clocks l2_clk] -to [get_clocks cpu_clk] \
    -file [file join $run_dir l2_to_cpu.rpt]
