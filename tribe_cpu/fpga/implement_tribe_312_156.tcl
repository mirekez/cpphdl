# Implement the synthesized TribeTest<4> checkpoint at the SmartNIC clocks.

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
    [file join $script_dir vivado_tribe_312_156]]]
set report_dir [file join $run_dir reports]
set checkpoint_dir [file join $run_dir checkpoints]
set cpu_period [expr {1000.0 / 312.0}]
set l2_period [expr {1000.0 / 156.0}]

file mkdir $report_dir
file mkdir $checkpoint_dir
open_checkpoint $synth_dcp

# Replace the exploratory clocks stored in the synthesis checkpoint and apply
# the protocol-aware mailbox constraints.
reset_timing
read_xdc [file join $script_dir tribe_312_156_constraints.xdc]

# Both clocks are phase aligned in Tribe's documented 2:1 topology.  Do not
# declare them asynchronous in this raw run: it intentionally exposes every
# CPU/L2 crossing that needs a protocol-aware exception.

opt_design -directive Explore
place_design -directive ExtraPostPlacementOpt
phys_opt_design -directive AggressiveExplore
write_checkpoint -force [file join $checkpoint_dir post_place.dcp]
route_design -directive AggressiveExplore
write_checkpoint -force [file join $checkpoint_dir post_route.dcp]

report_utilization -hierarchical -hierarchical_depth 5 \
    -file [file join $report_dir utilization_post_route.rpt]
report_timing_summary -delay_type min_max -max_paths 100 -report_unconstrained \
    -file [file join $report_dir timing_summary_post_route.rpt]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks cpu_clk] \
    -file [file join $report_dir timing_cpu_to_cpu.rpt]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks l2_clk] -to [get_clocks l2_clk] \
    -file [file join $report_dir timing_l2_to_l2.rpt]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks l2_clk] \
    -file [file join $report_dir timing_cpu_to_l2_raw.rpt]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks l2_clk] -to [get_clocks cpu_clk] \
    -file [file join $report_dir timing_l2_to_cpu_raw.rpt]
report_clock_interaction \
    -file [file join $report_dir clock_interaction_post_route.rpt]
report_cdc -details \
    -file [file join $report_dir cdc_post_route.rpt]
report_methodology \
    -file [file join $report_dir methodology_post_route.rpt]
report_drc \
    -file [file join $report_dir drc_post_route.rpt]

puts [format "TRIBE_TARGET_DONE cpu=%.6fns l2=%.6fns" $cpu_period $l2_period]
