# Continue a verified mapped module from its real placement/CTS checkpoint.
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set work $::env(REPEATER_WORK)
set setup_margin 0
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
read_db "$work/placed.odb"
read_sdc "$work/placed.sdc"
set_max_transition $transition_target [current_design]
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_propagated_clock [all_clocks]
add_global_connection -net VDD -inst_pattern .* -pin_pattern VDD -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern VSS -ground
global_connect
source "$::env(REPEATER_SOURCE)/clean_empty_nets.tcl"
clean_empty_constant_nets
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
global_route -guide_file "$work/core.guide"
estimate_parasitics -global_routing
repair_design -slew_margin 20 -cap_margin $cap_margin
global_route -start_incremental
detailed_placement
global_route -end_incremental
estimate_parasitics -global_routing
repair_timing -setup -setup_margin $setup_margin -max_iterations 100
repair_timing -hold -hold_margin 0.05 -max_iterations 100
# Hold/setup repair changes drive strengths and inserts new loads. Repair
# that network before committing the topology to detailed routing.
repair_design -slew_margin 20 -cap_margin $cap_margin
global_route -start_incremental
detailed_placement
global_route -end_incremental -guide_file "$work/core.guide"
# Repair the final hold/setup network, including newly inserted delay gates.
# Use real diffusion protection: routing jumpers alone left a cumulative
# violation in the independent KLayout deck after detailed routing.
repair_antennas sg13g2_antennanp -diode_only -iterations 5 -ratio_margin 20
if {$::env(REPEATER_TOP) == "ADCReceiveController"} {
    # Detailed routing exposed a long comparison-input antenna that the
    # global-route estimate missed. Protect these analog receiver boundaries.
    global_route -start_incremental
    source "$::env(REPEATER_SOURCE)/antenna_inputs.tcl"
    set protected [protect_input_delay_gates {comparisons_in*} "$work/input_antenna.csv"]
    if {$protected != 16} { error "ADC comparison-input protection is incomplete" }
    global_route -end_incremental -guide_file "$work/core.guide"
}
check_antennas -report_file "$work/global_antenna.rpt"
filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
global_connect
check_placement -verbose
clean_empty_constant_nets
write_db "$work/global_repaired.odb"
file mkdir "$work/route_snapshots"
set_debug_level DRT snapshot 1
detailed_route_debug -snapshot_dir "$work/route_snapshots"
detailed_route -disable_via_gen -output_drc "$work/route_drc.rpt" -output_maze "$work/route_maze.log"
define_process_corner -ext_model_index 0 RCnom
extract_parasitics -ext_model_file "$::env(REPEATER_TOOLS)/rcx_patterns.rules" -corner_cnt 1
write_spef "$work/core.spef"
read_spef -corner slow "$work/core.spef"
read_spef -corner fast "$work/core.spef"
report_checks -path_delay min_max -format full_clock_expanded > "$work/timing.rpt"
report_worst_slack -max > "$work/setup.rpt"
report_worst_slack -min > "$work/hold.rpt"
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
