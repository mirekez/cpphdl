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
proc clock_load_refresh {} {
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
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
    -fields {slew cap fanout input_pins} -digits 9 > "$work/before_paths.rpt"
source "$::env(RECEIVER_SOURCE)/receiver_clock_loads.tcl"
report_check_types -max_capacitance -violators > "$work/before_capacitance.rpt"
set f [open "$work/before_capacitance.rpt" r]
set text [read $f]
close $f
set names {}
set clock_nets [dict create]
foreach net [sta::find_all_clk_nets] {dict set clock_nets [$net getName] 1}
foreach line [split $text "\n"] {
    if {[regexp {^(\S+)\s+.*VIOLATED} $line -> pin]} {
        set net [get_name [get_nets -of_objects [get_pins $pin]]]
        if {[dict exists $clock_nets $net]} {lappend names $pin}
    }
}
if {![llength $names]} {error "no overloaded clock branch selected"}
receiver_clock_loads $names "$work/clock_loads.csv" receiver_clock_load
clock_load_refresh
# Added clock latency can change data hold relationships; repair and prove any
# inserted identity cells under the same original timing constraints.
for {set pass 0} {$pass < 3 && [sta::worst_slack_cmd min] < 0.1e-9} {incr pass} {
    repair_timing -hold -hold_margin 0.15 -setup_margin 0.1
    clock_load_refresh
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
