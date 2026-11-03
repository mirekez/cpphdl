# Diagnose excess short data buffers in a retained global-route checkpoint.
# No result here qualifies extracted timing or a physical implementation.
set work $::env(REPEATER_WORK)
set span_um $::env(RECEIVER_UNBUFFER_SPAN)
set rounds $::env(RECEIVER_UNBUFFER_ROUNDS)
set target $::env(RECEIVER_UNBUFFER_TARGET)
set electrical_repair $::env(RECEIVER_UNBUFFER_REPAIR)
set maximum_sinks 2
if {[info exists ::env(RECEIVER_UNBUFFER_SINKS)]} {set maximum_sinks $::env(RECEIVER_UNBUFFER_SINKS)}
if {![string is integer -strict $maximum_sinks] || $maximum_sinks < 1 || $maximum_sinks > 4} {
    error "invalid buffer sink bound"
}
set trim_hold 0
if {[info exists ::env(RECEIVER_TRIM_HOLD)]} {set trim_hold $::env(RECEIVER_TRIM_HOLD)}
if {$trim_hold ni {0 1}} {error "invalid hold-trim control"}
if {![string is integer -strict $rounds] || $rounds < (1 - $trim_hold) || $rounds > 16 ||
    ![string is double -strict $target] || !($target >= 0 && $target <= 2) ||
    ($trim_hold && $target <= 0) ||
    $electrical_repair ni {0 1}} {error "invalid unbuffer diagnostic controls"}
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$lib/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db $::env(RECEIVER_SEED)
if {[[ord::get_db_block] getName] != "PCSReceiver"} {error "receiver checkpoint required"}
read_sdc $::env(RECEIVER_SDC)
set_propagated_clock [all_clocks]
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
source "$::env(RECEIVER_SOURCE)/receiver_local_unbuffer.tcl"

proc receiver_unbuffer_report {name} {
    set prefix "$::env(REPEATER_WORK)/$name"
    report_worst_slack -max -digits 9 > "${prefix}_setup.rpt"
    report_worst_slack -min -digits 9 > "${prefix}_hold.rpt"
    report_check_types -max_slew -max_capacitance -max_fanout -violators > "${prefix}_electrical.rpt"
    report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
        -fields {slew cap fanout input_pins} -digits 9 > "${prefix}_paths.rpt"
    write_verilog "${prefix}.v"
    puts "CPPHDL unbuffer $name: setup [expr {[sta::worst_slack_cmd max] * 1e9}] ns, hold [expr {[sta::worst_slack_cmd min] * 1e9}] ns"
    flush stdout
}

proc receiver_unbuffer_refresh {} {
    # Rebuild every route after topology edits; incremental tracking started
    # after removal would miss deleted nets and could omit receiving pins.
    global_route
    estimate_parasitics -placement
    estimate_parasitics -global_routing
}

receiver_unbuffer_refresh
receiver_unbuffer_report before
for {set pass 1} {$pass <= $rounds} {incr pass} {
    set names {}
    foreach path [find_timing_paths -path_delay max -slack_max $target -group_path_count 200] {
        foreach point [get_property $path points] {
            foreach cell [get_cells -quiet -of_objects [get_property $point pin]] {
                set name [get_name $cell]
                if {[regexp {^(place|wire)[0-9]+$} $name] && $name ni $names && [llength $names] < 200} {
                    lappend names $name
                }
            }
        }
    }
    if {![llength [receiver_local_buffer_candidates $names $span_um 200 $maximum_sinks]]} {break}
    remove_receiver_local_buffers $names $span_um "$work/removed_$pass.csv" 200 $maximum_sinks
    receiver_unbuffer_refresh
    receiver_unbuffer_report "round_$pass"
    # Stop before further changes when the new network damages hold margin.
    # Keep the failing candidate for diagnosis; never adopt it as a pass.
    if {[sta::worst_slack_cmd min] < 0.1e-9} {break}
}
if {$trim_hold} {
    source "$::env(RECEIVER_SOURCE)/receiver_hold_trim.tcl"
    receiver_hold_trim $target 0.1 3 "$work/hold_trials.csv" \
        $::env(RECEIVER_HOLD_TRIALS) $::env(RECEIVER_HOLD_SECONDS)
    receiver_unbuffer_report hold
}
if {$electrical_repair} {
    # Repair only violations of the original interface limits. Extra slew or
    # capacitance margins would reinsert the overbuffering under diagnosis.
    source "$::env(REPEATER_SOURCE)/finish_timing.tcl"
    for {set repair 0} {$repair < 4} {incr repair} {
        repair_design -slew_margin 0 -cap_margin 0
        detailed_placement
        check_placement -verbose
        receiver_unbuffer_refresh
        set report "$work/repair_${repair}_electrical.rpt"
        report_check_types -max_slew -max_capacitance -max_fanout -violators > $report
        # IHP a221oi/xor/o21ai cells have no stronger variant. Explicit output
        # isolation handles loads that the generic resizer cannot repair.
        if {![isolate_single_strength_outputs $report unbuffer_$repair]} {break}
        detailed_placement
        check_placement -verbose
        receiver_unbuffer_refresh
    }
}
write_db "$work/candidate.odb"
write_sdc "$work/candidate.sdc"
receiver_unbuffer_report after
exit
