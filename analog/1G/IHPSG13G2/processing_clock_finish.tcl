# The admission pin presents an 80 fF external load. A 72 fF optimization
# target cannot repair that load and merely grows a chain of buffers. Keep
# the optimization target at 95 fF, within the unchanged 100 fF acceptance.
# Shared route-update, hold-cell and output-isolation helpers are loaded by
# the caller from finish_timing.tcl.
proc finish_processing_clock_timing {work} {
    # The caller has initialized DPL before global routing. Subsequent
    # legalization must stay inside incremental routing so RC state stays valid.
    trim_critical_hold_cells
    for {set pass 0} {$pass < 4} {incr pass} {
        set_max_transition 0.4 [current_design]
        set_max_fanout 6 [current_design]
        set_max_capacitance 0.1 [current_design]
        repair_design -slew_margin 10 -cap_margin 5
        finish_route_update
        # Preserve completed buffering/legalization before the next optimizer.
        # A later failure can resume here with fresh routing/parasitic state.
        write_db "$work/electrical_repaired_$pass.odb"
        write_sdc "$work/electrical_repaired_$pass.sdc"
        # Removing a buffer can invalidate OpenROAD's saved repair-path edge.
        # Keep those cells and use sizing, pin swaps and added buffering.
        # Rebuffering this compact macro produced long serial chains whose
        # estimated gain disappeared after legalization. Keep setup repair to
        # sizing/pin swaps; electrical and hold repair still add needed buffers.
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
    # The enlarged wire C is an optimization target, not a process corner.
    # Keep its result, then judge the global candidate using the original LEF.
    # Detailed routing and independent RC extraction remain mandatory afterward.
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
