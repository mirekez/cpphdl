# Close the actual post-antenna network. All replacements use real library
# cells; extracted timing, gate replay and full DRC/LVS remain mandatory.
proc finish_route_update {} {
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental
    estimate_parasitics -global_routing
}

proc trim_critical_hold_cells {} {
    set tried [dict create]
    for {set pass 0} {$pass < 3} {incr pass} {
        set candidates [dict create]
        foreach path [find_timing_paths -path_delay max -slack_max 0.15 -group_path_count 200] {
            foreach point [get_property $path points] {
                foreach cell [get_cells -quiet -of_objects [get_property $point pin]] {
                    if {[regexp {^sg13g2_dlygate4sd([123])_1$} [get_property $cell ref_name] -> strength]} {
                        dict set candidates [get_name $cell] $strength
                    }
                }
            }
        }
        set accepted 0
        dict for {name strength} $candidates {
            if {[dict exists $tried "$name/$strength"]} {continue}
            dict set tried "$name/$strength" 1
            set previous [sta::worst_slack_cmd max]
            set old sg13g2_dlygate4sd${strength}_1
            set reduced [expr {$strength - 1}]
            set replacement sg13g2_buf_1
            if {$reduced > 0} {set replacement sg13g2_dlygate4sd${reduced}_1}
            replace_cell $name $replacement
            # STA's internal time unit is seconds. Preserve at least 50 ps
            # estimated hold margin while reducing excessive slow-corner delay.
            if {[sta::worst_slack_cmd min] >= 0.05e-9 &&
                [sta::worst_slack_cmd max] >= $previous - 1e-12} {
                puts "Retained hold-cell replacement: $name $old -> $replacement"
                incr accepted
            } else {
                replace_cell $name $old
            }
        }
        if {$accepted == 0} {break}
    }
    # Direct STA cell replacement leaves dirty RC entries outside the resizer's
    # incremental guard. Rebuild annotations before entering that guard, then
    # restore global-route RC before any acceptance or further optimization.
    estimate_parasitics -placement
    estimate_parasitics -global_routing
    finish_route_update
}

proc isolate_single_strength_outputs {report pass} {
    set f [open $report r]
    set evidence [read $f]
    close $f
    set seen [dict create]
    set index 0
    foreach line [split $evidence "\n"] {
        if {![regexp {^(\S+)\s+0\.50.*VIOLATED} $line -> pin_name]} {continue}
        set pin [get_pins -quiet $pin_name]
        if {$pin == "" || [get_property $pin direction] != "output"} {continue}
        set cell [lindex [get_cells -of_objects $pin] 0]
        # These IHP functions have no stronger equivalent cell. A small real
        # buffer isolates their outputs from a large receiving-pin capacitance.
        if {![regexp {^sg13g2_(xn?or2|o21ai|a221oi)_1$} [get_property $cell ref_name]]} {continue}
        if {[dict exists $seen $pin_name]} {continue}
        dict set seen $pin_name 1
        set block [ord::get_db_block]
        set instance [$block findInst [get_name $cell]]
        set location [$instance getLocation]
        set dbu [$block getDbUnitsPerMicron]
        set x [expr {double([lindex $location 0]) / $dbu}]
        set y [expr {double([lindex $location 1]) / $dbu}]
        puts "Isolate loaded output: $pin_name"
        insert_buffer -buffer_cell sg13g2_buf_1 -net [get_nets -of_objects $pin] \
            -location [list $x $y] -buffer_name finish_slew_${pass}_$index \
            -net_name finish_slew_net_${pass}_$index
        incr index
    }
    return $index
}

proc finish_routed_timing {work} {
    # The caller has initialized DPL before global routing. Subsequent
    # legalization must stay inside incremental routing so RC state stays valid.
    trim_critical_hold_cells
    for {set pass 0} {$pass < 4} {incr pass} {
        set_max_transition 0.4 [current_design]
        set_max_fanout 6 [current_design]
        set_max_capacitance 0.08 [current_design]
        repair_design -slew_margin 10 -cap_margin 10
        finish_route_update
        # Preserve completed buffering/legalization before the next optimizer.
        # A later failure can resume here with fresh routing/parasitic state.
        write_db "$work/electrical_repaired_$pass.odb"
        write_sdc "$work/electrical_repaired_$pass.sdc"
        # Removing a buffer can invalidate OpenROAD's saved repair-path edge.
        # Setup rebuffering grew long serial chains whose estimated gain was
        # lost after legalization. Use sizing/pin swaps here; electrical and
        # hold repair still insert the buffers required by their constraints.
        repair_timing -setup -setup_margin 0.2 -max_iterations 1000 -max_passes 100 -skip_buffer_removal -skip_buffering
        repair_timing -hold -hold_margin 0.08 -max_iterations 100
        finish_route_update
        repair_antennas sg13g2_antennanp -diode_only -iterations 5 -ratio_margin 20
        global_connect
        # Antenna insertion also edits the database outside the resizer guard.
        # Clear its dirty entries, then regenerate actual route annotations.
        estimate_parasitics -placement
        estimate_parasitics -global_routing
        set_max_transition 0.5 [current_design]
        set_max_fanout 8 [current_design]
        set_max_capacitance 0.1 [current_design]
        set electrical "$work/finish_electrical_$pass.rpt"
        report_check_types -max_slew -max_capacitance -max_fanout -violators > $electrical
        write_db "$work/finish_stage_$pass.odb"
        write_sdc "$work/finish_stage_$pass.sdc"
        set isolated [isolate_single_strength_outputs $electrical $pass]
        if {$isolated == 0} {break}
        finish_route_update
    }
    # Enlarged wire C guides optimization; it is not a characterized corner.
    # Preserve that result and screen the candidate with nominal LEF RC.
    # Independent detailed-route extraction remains the acceptance criterion.
    report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/optimization_timing.rpt"
    report_worst_slack -max -digits 9 > "$work/optimization_setup.rpt"
    if {[info exists ::env(REPEATER_ROUTING_CAPACITANCE_MARGIN)] &&
        $::env(REPEATER_ROUTING_CAPACITANCE_MARGIN) != 1} {
        apply_routing_capacitance_margin 1 "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef"
        estimate_parasitics -global_routing
    }
    report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/global_timing.rpt"
    report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
    report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
    report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/global_electrical.rpt"
    check_antennas -report_file "$work/global_antenna.rpt"
    global_connect
    write_db "$work/finished_global.odb"
    write_sdc "$work/finished_global.sdc"
    set f [open "$work/global_electrical.rpt" r]
    set electrical [read $f]
    close $f
    if {[sta::worst_slack_cmd max] < 0 || [sta::worst_slack_cmd min] < 0 ||
        [string first "VIOLATED" $electrical] >= 0} {
        error "final global network fails timing/electrical limits; inspect retained reports"
    }
}
