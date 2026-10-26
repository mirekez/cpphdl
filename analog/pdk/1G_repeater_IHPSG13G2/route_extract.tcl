# Continue a verified mapped module from its real placement/CTS checkpoint.
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set work $::env(REPEATER_WORK)
set setup_margin 0
set setup_repair_options {}
if {[info exists ::env(REPEATER_TOP)] && $::env(REPEATER_TOP) in {ProcessingClockControl PCSReceiver}} {
    set setup_repair_options {-skip_buffering -skip_buffer_removal}
}
set cap_margin 10
set transition_target 0.3
if {[info exists ::env(REPEATER_ROUTE_TRANSITION_TARGET)]} {
    set transition_target $::env(REPEATER_ROUTE_TRANSITION_TARGET)
}
if {[info exists ::env(REPEATER_ROUTE_SETUP_MARGIN)]} {
    set setup_margin $::env(REPEATER_ROUTE_SETUP_MARGIN)
}
if {[info exists ::env(REPEATER_ROUTE_CAP_MARGIN)]} {
    set cap_margin $::env(REPEATER_ROUTE_CAP_MARGIN)
}
set routing_threads 1
if {[info exists ::env(REPEATER_ROUTE_THREADS)]} {
    set routing_threads $::env(REPEATER_ROUTE_THREADS)
}
set_thread_count $routing_threads
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
set input_database placed
if {[info exists ::env(REPEATER_TIMING_PREPARED)] && $::env(REPEATER_TIMING_PREPARED) == "1"} {
    set input_database pre_route
}
read_db "$work/$input_database.odb"
read_sdc "$work/$input_database.sdc"
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
set routing_capacitance_margin 1
if {[info exists ::env(REPEATER_ROUTING_CAPACITANCE_MARGIN)]} {
    set routing_capacitance_margin $::env(REPEATER_ROUTING_CAPACITANCE_MARGIN)
}
apply_routing_capacitance_margin $routing_capacitance_margin "$libroot/lef/sg13g2_tech.lef"
set_propagated_clock [all_clocks]
add_global_connection -net VDD -inst_pattern .* -pin_pattern VDD -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern VSS -ground
global_connect
if {[info exists ::env(REPEATER_STAGE)] && $::env(REPEATER_STAGE) == "prepare_route"} {
    # Hold legalization can move a marginal setup path below zero. Build
    # setup reserve in the saved placement before routed loads are introduced.
    # These are optimization targets; final extracted acceptance is unchanged.
    estimate_parasitics -placement
    repair_timing -setup -setup_margin $::env(REPEATER_PRE_ROUTE_SETUP_MARGIN) -max_iterations 100 {*}$setup_repair_options
    detailed_placement
    estimate_parasitics -placement
    if {[info exists ::env(REPEATER_FINAL_HOLD_MARGIN)] && $::env(REPEATER_FINAL_HOLD_MARGIN) > 0} {
        repair_timing -hold -hold_margin $::env(REPEATER_FINAL_HOLD_MARGIN) -max_iterations 100
        detailed_placement
        estimate_parasitics -placement
    }
    report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/pre_route_timing.rpt"
    report_worst_slack -max -digits 9 > "$work/pre_route_setup.rpt"
    report_worst_slack -min -digits 9 > "$work/pre_route_hold.rpt"
    global_connect
    write_db "$work/pre_route.odb"
    write_sdc "$work/pre_route.sdc"
    # Restart before global routing: placement dirty-net state cannot be
    # carried into the global-route estimator's incremental updates.
    exit
}
set_max_transition $transition_target [current_design]
source "$::env(REPEATER_SOURCE)/clean_empty_nets.tcl"
clean_empty_constant_nets
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
global_route -guide_file "$work/core.guide"
estimate_parasitics -global_routing
repair_design -slew_margin 20 -cap_margin $cap_margin
# The large receiver can change tens of thousands of nets in this repair.
# Retain the legalized topology before routing it, and rebuild its routes
# instead of carrying a large incremental dirty-net set into the router.
# Other modules retain the incremental path used by their qualified flows.
if {[info exists ::env(REPEATER_TOP)] && $::env(REPEATER_TOP) == "PCSReceiver"} {
    detailed_placement
    global_connect
    write_db "$work/resized_global.odb"
    write_sdc "$work/resized_global.sdc"
    puts "CPPHDL: receiver resized network checkpoint saved; rebuilding global routes"
    flush stdout
    global_route -guide_file "$work/core.guide" -verbose
} else {
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental
}
estimate_parasitics -global_routing
repair_timing -setup -setup_margin $setup_margin -max_iterations 100 {*}$setup_repair_options
repair_timing -hold -hold_margin 0.05 -max_iterations 100
# Hold/setup repair changes drive strengths and inserts new loads. Repair
# that network before committing the topology to detailed routing.
repair_design -slew_margin 20 -cap_margin $cap_margin
global_route -start_incremental
detailed_placement
global_route -end_incremental -guide_file "$work/core.guide"
if {[info exists ::env(REPEATER_FINAL_HOLD_MARGIN)] && $::env(REPEATER_FINAL_HOLD_MARGIN) > 0} {
    # Use the repaired and legalized network, including actual route estimates.
    # A final drive-strength repair must not silently undo inserted hold delay.
    estimate_parasitics -global_routing
    repair_timing -hold -hold_margin $::env(REPEATER_FINAL_HOLD_MARGIN) -max_iterations 100
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental -guide_file "$work/core.guide"
    estimate_parasitics -global_routing
    report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/global_timing.rpt"
    report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
    report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
}
# Repair the final hold/setup network, including newly inserted delay gates.
# Use real diffusion protection: routing jumpers alone left a cumulative
# violation in the independent KLayout deck after detailed routing.
repair_antennas sg13g2_antennanp -diode_only -iterations 5 -ratio_margin 20
if {[info exists ::env(REPEATER_ADC_COMPARISON_PORTS)] && $::env(REPEATER_ADC_COMPARISON_PORTS) > 0} {
    # Detailed routing exposed a long comparison-input antenna that the
    # global-route estimate missed. Protect these analog receiver boundaries.
    global_route -start_incremental
    source "$::env(REPEATER_SOURCE)/antenna_inputs.tcl"
    set protected [protect_input_delay_gates {comparisons_in*} "$work/input_antenna.csv"]
    if {$protected != $::env(REPEATER_ADC_COMPARISON_PORTS)} { error "ADC comparison-input protection is incomplete" }
    global_route -end_incremental -guide_file "$work/core.guide"
}
check_antennas -report_file "$work/global_antenna.rpt"
if {[info exists ::env(REPEATER_FINAL_NETWORK_REPAIR)] && $::env(REPEATER_FINAL_NETWORK_REPAIR) == "1"} {
    source "$::env(REPEATER_SOURCE)/finish_timing.tcl"
    if {[info exists ::env(REPEATER_FINAL_TIMING_SCRIPT)] && $::env(REPEATER_FINAL_TIMING_SCRIPT) != ""} {
        source $::env(REPEATER_FINAL_TIMING_SCRIPT)
        finish_processing_clock_timing $work
    } else {
        finish_routed_timing $work
    }
}
filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
global_connect
check_placement -verbose
clean_empty_constant_nets
write_db "$work/global_repaired.odb"
write_sdc "$work/global_repaired.sdc"
if {[info exists ::env(REPEATER_STAGE)] && $::env(REPEATER_STAGE) == "global_route"} {
    exit
}
file mkdir "$work/route_snapshots"
set_debug_level DRT snapshot 1
detailed_route_debug -snapshot_dir "$work/route_snapshots"
detailed_route -disable_via_gen -output_drc "$work/route_drc.rpt" -output_maze "$work/route_maze.log"
define_process_corner -ext_model_index 0 RCnom
apply_routing_capacitance_margin 1 "$libroot/lef/sg13g2_tech.lef"
extract_parasitics -ext_model_file "$::env(REPEATER_TOOLS)/rcx_patterns.rules" -corner_cnt 1
write_spef "$work/core.spef"
read_spef -corner slow "$work/core.spef"
read_spef -corner fast "$work/core.spef"
report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/timing.rpt"
report_worst_slack -max -digits 9 > "$work/setup.rpt"
report_worst_slack -min -digits 9 > "$work/hold.rpt"
# Sizing uses the tighter routing target. Qualification uses the explicit
# interface budget, still tighter than the library's 2.5074 ns slow-corner limit.
set_max_transition $::env(REPEATER_MAX_TRANSITION) [current_design]
set_max_fanout 8 [current_design]
set_max_capacitance $::env(REPEATER_MAX_CAPACITANCE) [current_design]
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/electrical.rpt"
write_def "$work/core.def"
write_db "$work/core.odb"
write_verilog -include_pwr_gnd "$work/physical.v"
write_verilog "$work/postroute.v"
write_sdc "$work/physical.sdc"
write_sdf -scene slow "$work/core.sdf"
write_cdl -include_fillers -masters "$libroot/cdl/sg13g2_stdcell.cdl" "$work/core.cdl"
