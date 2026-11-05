set work $::env(REPEATER_WORK)
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$::env(RECEIVER_SEED)"
read_sdc "$::env(RECEIVER_SDC)"
set_propagated_clock [all_clocks]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
apply_routing_capacitance_margin 1.75 "$libroot/lef/sg13g2_tech.lef"
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
proc compact_refresh {} {
    detailed_placement
    check_placement -verbose
    global_route
    estimate_parasitics -placement
    estimate_parasitics -global_routing
}
check_placement -verbose
global_route
estimate_parasitics -global_routing
write_verilog "$work/before.v"
report_worst_slack -max -digits 9 > "$work/before_setup.rpt"
report_worst_slack -min -digits 9 > "$work/before_hold.rpt"
source "$::env(RECEIVER_SOURCE)/receiver_local_unbuffer.tcl"
set rounds $::env(RECEIVER_COMPACT_ROUNDS)
if {![string is integer -strict $rounds] || $rounds < 1 || $rounds > 8} {
    error "compact rounds must be in [1, 8]"
}
set previous_setup [sta::worst_slack_cmd max]
for {set round 0} {$round < $rounds} {incr round} {
    set names {}
    foreach path [find_timing_paths -path_delay max -slack_max 0.15 -group_path_count 100] {
        foreach point [get_property $path points] {
            foreach cell [get_cells -quiet -of_objects [get_property $point pin]] {
                set name [get_name $cell]
                if {[regexp {^(place|wire)[0-9]+$} $name] && $name ni $names && [llength $names] < 200} {
                    lappend names $name
                }
            }
        }
    }
    set span $::env(RECEIVER_COMPACT_SPAN)
    if {![llength [receiver_local_buffer_candidates $names $span 200 4]]} {break}
    remove_receiver_local_buffers $names $span "$work/removed_$round.csv" 200 4
    compact_refresh
    report_worst_slack -max -digits 9 > "$work/round_${round}_setup.rpt"
    report_worst_slack -min -digits 9 > "$work/round_${round}_hold.rpt"
    # Reject this independent candidate at the first timing regression. The
    # origin checkpoint is immutable; a failed experiment must not replace it.
    set current_setup [sta::worst_slack_cmd max]
    if {$current_setup < $previous_setup - 0.005e-9} {
        error "compaction regressed setup by more than 5 ps; retain origin checkpoint"
    }
    set previous_setup $current_setup
    if {[sta::worst_slack_cmd max] >= 0.15e-9 || [sta::worst_slack_cmd min] < 0.1e-9} { break }
}
repair_design
compact_refresh
for {set pass 0} {$pass < 3 && [sta::worst_slack_cmd min] < 0.1e-9} {incr pass} {
    repair_timing -hold -hold_margin 0.15 -setup_margin 0.1
    compact_refresh
}
write_verilog "$work/repaired.v"
puts "RECEIVER_FINAL_ROUTE_CHECK_BEGIN"
global_route
estimate_parasitics -placement
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/margin_setup.rpt"
report_worst_slack -min -digits 9 > "$work/margin_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/margin_electrical.rpt"
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
puts "RECEIVER_FINAL_ROUTE_CHECK_END"
exit
