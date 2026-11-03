set work $::env(REPEATER_WORK)
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
source "$::env(RECEIVER_SOURCE)/receiver_checkpoint_choice.tcl"
source "$::env(RECEIVER_SOURCE)/receiver_timing_tradeoffs.tcl"

proc data_restore {database} {
    read_db $database
    read_sdc "$::env(RECEIVER_SDC)"
    set_propagated_clock [all_clocks]
    set_max_transition 0.5 [current_design]
    set_max_capacitance 0.1 [current_design]
    set_max_fanout 8 [current_design]
    set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
    set_global_routing_layer_adjustment Metal2-Metal5 0.30
}

proc data_refresh {} {
    detailed_placement
    check_placement -verbose
    global_route
    estimate_parasitics -placement
    estimate_parasitics -global_routing
}

proc data_measure {prefix} {
    global libroot
    set values [dict create]
    foreach {name factor} {nominal 1 margin 1.75} {
        apply_routing_capacitance_margin $factor "$libroot/lef/sg13g2_tech.lef"
        estimate_parasitics -placement
        estimate_parasitics -global_routing
        dict set values ${name}_setup [expr {[sta::worst_slack_cmd max] * 1e9}]
        dict set values ${name}_hold [expr {[sta::worst_slack_cmd min] * 1e9}]
        dict set values ${name}_tns [expr {[sta::total_negative_slack_cmd max] * 1e9}]
        set report "${prefix}_${name}_electrical.rpt"
        report_check_types -max_slew -max_capacitance -max_fanout -violators > $report
        set f [open $report r]
        dict set values ${name}_electrical [expr {![string match *VIOLATED* [read $f]]}]
        close $f
    }
    return $values
}

data_restore "$::env(RECEIVER_SEED)"
apply_routing_capacitance_margin 1.75 "$libroot/lef/sg13g2_tech.lef"
check_placement -verbose
global_route
estimate_parasitics -global_routing
write_verilog "$work/before.v"
report_worst_slack -max -digits 9 > "$work/before_setup.rpt"
report_worst_slack -min -digits 9 > "$work/before_hold.rpt"
set before [data_measure "$work/before"]
write_db "$work/retained.odb"
write_sdc "$work/retained.sdc"
set original_cells [dict create]
foreach instance [[ord::get_db_block] getInsts] {
    dict set original_cells [$instance getName] [list [[$instance getMaster] getName] \
        [$instance getOrient] [$instance getLocation] [$instance getPlacementStatus]]
}
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
    -fields {slew cap fanout input_pins} -digits 9 > "$work/before_paths.rpt"
exec $::env(RECEIVER_PYTHON) "$::env(RECEIVER_SOURCE)/receiver_data_trials.py" \
    --paths "$work/before_paths.rpt" --netlist "$work/before.v" \
    --models "$::env(RECEIVER_MODELS)" --output "$work/data_trials.tcl"
source "$work/data_trials.tcl"
set limit $::env(RECEIVER_DATA_TRIALS)
if {![string is integer -strict $limit] || $limit < 1 || $limit > 200} {
    error "data trial limit must be in [1, 200]"
}
receiver_timing_tradeoffs [lrange $data_trials 0 [expr {$limit - 1}]] "$work/data_trials.csv" 0.1 global_routing
data_refresh
set after [data_measure "$work/trial"]
set accepted [receiver_checkpoint_improves $before $after]
set f [open "$work/selection.txt" w]
puts $f "before {$before}"
puts $f "trial {$after}"
puts $f "accepted $accepted"
close $f
if {$accepted} {
    # Only a freshly legalized, routed and screened improvement replaces this
    # checkpoint. The independent caller still proves exact Boolean endpoints.
    write_db "$work/retained.odb"
    write_sdc "$work/retained.sdc"
} else {
    # read_db cannot replace a populated OpenROAD database. This stage changes
    # only cell masters and placement, so restore those exact attributes here.
    # retained.odb remains available for a later fresh-process continuation.
    set block [ord::get_db_block]
    if {[llength [$block getInsts]] != [dict size $original_cells]} {
        error "data sizing changed the instance set; cannot restore in place"
    }
    dict for {name saved} $original_cells {
        set instance [$block findInst $name]
        if {$instance == "NULL"} {error "data sizing removed an original cell"}
        lassign $saved master orientation location status
        if {[[$instance getMaster] getName] != $master} {replace_cell $name $master}
        $instance setOrient $orientation
        $instance setLocation {*}$location
        $instance setPlacementStatus $status
    }
    check_placement -verbose
}
puts "RECEIVER_FINAL_ROUTE_CHECK_BEGIN"
apply_routing_capacitance_margin 1.75 "$libroot/lef/sg13g2_tech.lef"
global_route
estimate_parasitics -placement
estimate_parasitics -global_routing
if {!$accepted && (abs([sta::worst_slack_cmd max] * 1e9 - [dict get $before margin_setup]) > 0.001 ||
                  abs([sta::worst_slack_cmd min] * 1e9 - [dict get $before margin_hold]) > 0.001)} {
    error "restored physical checkpoint timing differs by more than 1 ps"
}
write_verilog "$work/repaired.v"
report_worst_slack -max -digits 9 > "$work/margin_setup.rpt"
report_worst_slack -min -digits 9 > "$work/margin_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/margin_electrical.rpt"
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
    -fields {slew cap fanout input_pins} -digits 9 > "$work/margin_paths.rpt"
apply_routing_capacitance_margin 1 "$libroot/lef/sg13g2_tech.lef"
estimate_parasitics -placement
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/global_electrical.rpt"
write_verilog "$work/global.v"
write_guides "$work/global.guide"
write_db "$work/global.odb"
puts "RECEIVER_FINAL_ROUTE_CHECK_END"
exit
