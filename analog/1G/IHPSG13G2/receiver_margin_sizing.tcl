# Enlarge only the optimization wire capacitance; final RC remains nominal.
set work $::env(REPEATER_WORK)
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$::env(RECEIVER_SEED)"
if {[[ord::get_db_block] getName] != "PCSReceiver"} { error "receiver-only margin sizing" }
read_sdc "$::env(RECEIVER_SDC)"
set_propagated_clock [all_clocks]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
apply_routing_capacitance_margin 1 "$libroot/lef/sg13g2_tech.lef"
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
global_route -guide_file "$work/global.guide"
estimate_parasitics -global_routing
write_verilog "$work/before.v"
report_worst_slack -max -digits 9 > "$work/before_setup.rpt"
report_worst_slack -min -digits 9 > "$work/before_hold.rpt"
apply_routing_capacitance_margin 1.5 "$libroot/lef/sg13g2_tech.lef"
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/margin_before_setup.rpt"
repair_timing -setup -sequence sizeup -setup_margin 0.25 -max_iterations 500 -max_passes 50 \
    -skip_last_gasp -skip_vt_swap -skip_crit_vt_swap -verbose
write_verilog "$work/sized.v"
# Legalization may change the repair's internal timing estimates. Rebuild
# routing and STA before accepting either the margin or nominal estimates.
estimate_parasitics -placement
estimate_parasitics -global_routing
global_route -start_incremental
detailed_placement
global_route -end_incremental
check_placement -verbose
estimate_parasitics -global_routing
write_verilog "$work/clock_before.v"
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
    -fields {slew cap fanout input_pins} -digits 9 > "$work/pre_clock_paths.rpt"
source "$::env(RECEIVER_SOURCE)/receiver_clock_path_trials.tcl"
source "$::env(RECEIVER_SOURCE)/receiver_timing_tradeoffs.tcl"
set trials [receiver_clock_path_trials "$work/pre_clock_paths.rpt"]
set trial_limit $::env(RECEIVER_TRIAL_LIMIT)
if {![string is integer -strict $trial_limit] || $trial_limit < 1 || $trial_limit > 200} {
    error "trial limit must be 1 to 200"
}
receiver_timing_tradeoffs [lrange $trials 0 [expr {$trial_limit - 1}]] \
    "$work/clock_trials.csv" 0.1 global_routing
write_verilog "$work/clock_after.v"
estimate_parasitics -placement
estimate_parasitics -global_routing
global_route -start_incremental
detailed_placement
global_route -end_incremental
check_placement -verbose
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/margin_setup.rpt"
report_worst_slack -min -digits 9 > "$work/margin_hold.rpt"
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
    -fields {slew cap fanout input_pins} -digits 9 > "$work/margin_paths.rpt"
apply_routing_capacitance_margin 1 "$libroot/lef/sg13g2_tech.lef"
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/global_electrical.rpt"
write_verilog "$work/global.v"
write_guides "$work/global.guide"
write_db "$work/global.odb"
exit
