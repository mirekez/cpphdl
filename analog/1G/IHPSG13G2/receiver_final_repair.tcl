# Repair the measured receiver, then repair loading again after drive sizing.
# The enlarged wire C is an optimization probe, never final extracted timing.
set work $::env(REPEATER_WORK)
set slew_margin $::env(RECEIVER_SLEW_MARGIN)
set cap_margin $::env(RECEIVER_CAP_MARGIN)
set rebuffer $::env(RECEIVER_REBUFFER)
if {$rebuffer ni {0 1}} { error "buffer replacement must be explicitly enabled or disabled" }
set data_sizing $::env(RECEIVER_DATA_SIZING)
if {$data_sizing ni {0 1}} { error "data sizing must be explicitly enabled or disabled" }
set trial_limit $::env(RECEIVER_TRIAL_LIMIT)
if {![string is integer -strict $trial_limit] || $trial_limit < 1 || $trial_limit > 200} {
    error "trial limit must be 1 to 200"
}
set setup_sequence sizeup
if {$rebuffer} { set setup_sequence "unbuffer,sizeup,buffer" }
foreach margin [list $slew_margin $cap_margin] {
    if {![string is double -strict $margin] || !($margin >= 0 && $margin <= 40)} {
        error "extra electrical margin must be finite and between 0 and 40 percent"
    }
}
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$::env(RECEIVER_SEED)"
if {[[ord::get_db_block] getName] != "PCSReceiver"} { error "receiver-only final repair" }
read_sdc "$::env(RECEIVER_SDC)"
set_propagated_clock [all_clocks]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
write_guides "$work/restored.guide"
read_guides "$work/restored.guide"
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
write_verilog "$work/before.v"
remove_fillers
write_verilog "$work/unfilled.v"
foreach corner {slow fast} { read_spef -corner $corner "$::env(RECEIVER_SPEF)" }
report_worst_slack -max -digits 9 > "$work/before_setup.rpt"
report_worst_slack -min -digits 9 > "$work/before_hold.rpt"
# The imported SPEF checks the input only. Initialize the routing estimator
# before repair; importing SPEF alone does not initialize its source state.
set block [ord::get_db_block]
foreach net [$block getNets] {
    if {![$net isSpecial] && [$net getWire] != "NULL"} { odb::dbWire_destroy [$net getWire] }
}
source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
apply_routing_capacitance_margin 1.75 "$libroot/lef/sg13g2_tech.lef"
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
detailed_placement
check_placement -verbose
global_route -guide_file "$work/global.guide"
estimate_parasitics -global_routing

proc receiver_relegalize {} {
    # Repair can insert buffers before this procedure is entered. Starting
    # incremental tracking here misses those topology changes and can retain
    # routes that omit a new pin. Rebuild every signal route after legalization.
    detailed_placement
    check_placement -verbose
    global_route
    # The pinned estimator's global-RC refresh leaves its invalid-net set
    # intact. Placement estimation clears it; replace those estimates with
    # freshly rebuilt global RC before any optimization or reported timing.
    estimate_parasitics -placement
    estimate_parasitics -global_routing
}

source "$::env(REPEATER_SOURCE)/finish_timing.tcl"
proc receiver_repair_loading {label} {
    # IHP a221oi/xor/o21ai have no stronger master. If generic repair leaves
    # an overloaded output, isolate it with a real buffer, legalize, and rebuild
    # RC before the next decision. Acceptance still uses the original limits.
    for {set pass 0} {$pass < 4} {incr pass} {
        # Handle known single-strength outputs first. Generic repair can then
        # size the inserted buffer for its actual downstream wire/pin load.
        set before "$::work/${label}_${pass}_before_electrical.rpt"
        report_check_types -max_slew -max_capacitance -max_fanout -violators > $before
        if {[isolate_single_strength_outputs $before "${label}_${pass}_before"]} {
            receiver_relegalize
        }
        repair_design -slew_margin $::slew_margin -cap_margin $::cap_margin -verbose
        receiver_relegalize
        set report "$::work/${label}_${pass}_electrical.rpt"
        report_check_types -max_slew -max_capacitance -max_fanout -violators > $report
        if {![isolate_single_strength_outputs $report "${label}_$pass"]} {return}
        receiver_relegalize
    }
}

receiver_repair_loading initial
if {$rebuffer} { source "$::env(RECEIVER_SOURCE)/receiver_local_unbuffer.tcl" }
for {set round 1} {$round <= 2} {incr round} {
    if {$rebuffer} {
        # The native unbuffer move can reject every small improvement while
        # the complete path still misses its setup target. Try a bounded set
        # of short data buffers together, then rebuild routes and check both
        # setup and hold. This is a candidate transformation, never a pass.
        report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
            -fields {slew cap fanout input_pins} -digits 9 > "$work/local_${round}_paths.rpt"
        set f [open "$work/local_${round}_paths.rpt" r]
        set paths [read $f]
        close $f
        set names {}
        foreach pin [regexp -all -inline {(?:place|wire)[0-9]+/} $paths] {
            set name [string trimright $pin /]
            if {$name ni $names} { lappend names $name }
            if {[llength $names] >= 200} { break }
        }
        if {[llength [receiver_local_buffer_candidates $names 60]] > 0} {
            remove_receiver_local_buffers $names 60 "$work/local_${round}_removed.csv"
            receiver_relegalize
        }
    }
    repair_timing -setup -sequence $setup_sequence -setup_margin 0.35 -max_iterations 500 -max_passes 50 \
        -skip_last_gasp -skip_vt_swap -skip_crit_vt_swap -verbose
    receiver_relegalize
    if {$rebuffer} {
        # Removing a data buffer can improve setup while reducing hold slack.
        # Restore hold margin on the changed routes before accepting the ECO.
        repair_timing -hold -hold_margin 0.15 -setup_margin 0.1
        receiver_relegalize
    }
    # Larger input pins can overload the preceding stage. Repair the new load
    # using the enlarged-C model, then remeasure after placement and routing.
    receiver_repair_loading "round_$round"
    if {$data_sizing} {
        # Stronger buffers can overload the preceding gate. Try both drive
        # directions against the current RC, accepting only improved setup/TNS
        # with hold margin. Rebuild and repair the changed loads afterward.
        write_verilog "$work/data_${round}_before.v"
        report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
            -fields {slew cap fanout input_pins} -digits 9 > "$work/data_${round}_paths.rpt"
        exec $::env(RECEIVER_PYTHON) "$::env(RECEIVER_SOURCE)/receiver_data_trials.py" \
            --paths "$work/data_${round}_paths.rpt" --netlist "$work/data_${round}_before.v" \
            --models $::env(RECEIVER_CELLS) --output "$work/data_${round}_trials.tcl"
        source "$work/data_${round}_trials.tcl"
        source "$::env(RECEIVER_SOURCE)/receiver_timing_tradeoffs.tcl"
        receiver_timing_tradeoffs [lrange $data_trials 0 [expr {$trial_limit - 1}]] \
            "$work/data_${round}_trials.csv" 0.1 global_routing
        receiver_relegalize
        receiver_repair_loading "data_$round"
    }
    report_worst_slack -max -digits 9 > "$work/round_${round}_setup.rpt"
    report_worst_slack -min -digits 9 > "$work/round_${round}_hold.rpt"
}
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
    -fields {slew cap fanout input_pins} -digits 9 > "$work/pre_clock_paths.rpt"
source "$::env(RECEIVER_SOURCE)/receiver_clock_path_trials.tcl"
source "$::env(RECEIVER_SOURCE)/receiver_timing_tradeoffs.tcl"
set trials [receiver_clock_path_trials "$work/pre_clock_paths.rpt"]
receiver_timing_tradeoffs [lrange $trials 0 [expr {$trial_limit - 1}]] \
    "$work/clock_trials.csv" 0.1 global_routing
receiver_relegalize
receiver_repair_loading final
puts "RECEIVER_FINAL_ROUTE_CHECK_BEGIN"
receiver_relegalize
write_verilog "$work/repaired.v"
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
