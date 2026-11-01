# All paths arrive through the environment, so project paths are not Tcl code.
if {[info exists ::env(REPEATER_STAGE)] && $::env(REPEATER_STAGE) == "route"} {
    source "$::env(REPEATER_SOURCE)/route_extract.tcl"
    exit
}
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set work $::env(REPEATER_WORK)
set design_top RepeaterCore
set die_size 120
set constraints "$::env(REPEATER_SOURCE)/constraints.sdc"
if {[info exists ::env(REPEATER_TOP)]} {
    set design_top $::env(REPEATER_TOP)
    set die_size $::env(REPEATER_DIE_SIZE)
    set constraints $::env(REPEATER_CONSTRAINTS)
}
set_thread_count 2
set optimization_fanout 4
set placement_density 0.65
set placement_options {}
if {[info exists ::env(REPEATER_PLACEMENT_DENSITY)]} {
    set placement_density $::env(REPEATER_PLACEMENT_DENSITY)
}
if {[info exists ::env(REPEATER_TIMING_DRIVEN_PLACEMENT)] && $::env(REPEATER_TIMING_DRIVEN_PLACEMENT) == "1"} {
    # Cluster the receiver's late arithmetic paths before CTS and routing.
    # The floorplan must leave space for congestion-driven spreading.
    set placement_options {-timing_driven -routability_driven -timing_driven_net_weight_max 5 -timing_driven_nets_percentage 20}
}
if {[info exists ::env(REPEATER_OPTIMIZATION_FANOUT)]} {
    set optimization_fanout $::env(REPEATER_OPTIMIZATION_FANOUT)
}
read_lef "$work/route_tech.lef"
read_lef "$libroot/lef/sg13g2_stdcell.lef"
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_verilog "$work/mapped.v"
link_design $design_top
read_sdc $constraints
set setup_repair_options {}
if {$design_top == "ProcessingClockControl"} {
    set setup_repair_options {-skip_buffering -skip_buffer_removal}
}
if {[info exists ::env(REPEATER_LOGIC_REPAIR_SCRIPT)] && $::env(REPEATER_LOGIC_REPAIR_SCRIPT) != ""} {
    source $::env(REPEATER_LOGIC_REPAIR_SCRIPT)
    prepare_processing_clock_output
}
initialize_floorplan -die_area [list 0 0 $die_size $die_size] \
    -core_area [list 20 20 [expr {$die_size - 20}] [expr {$die_size - 20}]] -site CoreSite
make_tracks
add_global_connection -net VDD -inst_pattern .* -pin_pattern VDD -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern VSS -ground
global_connect
set_voltage_domain -name CORE -power VDD -ground VSS
define_pdn_grid -name core -voltage_domains CORE -pins {Metal4}
add_pdn_stripe -grid core -layer Metal1 -width 0.44 -followpins
add_pdn_stripe -grid core -layer Metal4 -width 1.2 -pitch 20 -offset 5
add_pdn_connect -grid core -layers {Metal1 Metal4}
pdngen
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
place_pins -hor_layers Metal3 -ver_layers Metal2
if {[info exists ::env(REPEATER_OUTPUT_BUFFER_CELL)] && $::env(REPEATER_OUTPUT_BUFFER_CELL) != ""} {
    # Isolate the converter's feedback registers from external translator
    # charge. These real output cells remain in timing, power, DRC and LVS.
    buffer_ports -outputs -buffer_cell $::env(REPEATER_OUTPUT_BUFFER_CELL)
}
if {$design_top != "RepeaterCore"} {
    set_placement_padding -global -left 1 -right 1
    set_max_fanout $optimization_fanout [current_design]
    set preparation_capacitance 0.01
    set optimization_capacitance 0.03
    if {[info exists ::env(REPEATER_PREPARATION_CAPACITANCE)]} {
        set preparation_capacitance $::env(REPEATER_PREPARATION_CAPACITANCE)
        set optimization_capacitance $::env(REPEATER_OPTIMIZATION_CAPACITANCE)
    }
    set_max_capacitance $preparation_capacitance [current_design]
    set_max_transition 0.3 [current_design]
    repair_design -pre_placement
    global_placement -density $placement_density {*}$placement_options
    # Use a physical interconnect budget after cell-only fanout preparation.
    # Individual Liberty pin limits still apply when they are more restrictive.
    set_max_capacitance $optimization_capacitance [current_design]
} else {
    set_placement_padding -global -left 2 -right 2
    global_placement -density 0.55
}
detailed_placement
repair_tie_fanout -separation 0 sg13g2_tiehi/L_HI
repair_tie_fanout -separation 0 sg13g2_tielo/L_LO
if {$design_top != "RepeaterCore"} {
    # Bound clock branching and wire loading as well as the data network.
    # An unconstrained shallow tree can drive sixteen loads despite the IHP
    # Liberty default maximum fanout of eight.
    clock_tree_synthesis -root_buf sg13g2_buf_4 -buf_list {sg13g2_buf_2 sg13g2_buf_4 sg13g2_buf_8} \
        -sink_clustering_enable -sink_clustering_size 4 -sink_clustering_max_diameter 40 \
        -distance_between_buffers $::env(REPEATER_CLOCK_DISTANCE) \
        -branching_point_buffers_distance $::env(REPEATER_CLOCK_BRANCH_DISTANCE) -balance_levels
    repair_clock_nets -max_wire_length $::env(REPEATER_CLOCK_DISTANCE)
} else {
    clock_tree_synthesis -root_buf sg13g2_buf_4 -buf_list {sg13g2_buf_2 sg13g2_buf_4 sg13g2_buf_8}
    repair_clock_nets
}
if {$design_top != "RepeaterCore"} {
    set_propagated_clock [all_clocks]
}
if {[info exists ::env(REPEATER_CLOCK_REPAIR_SCRIPT)] && $::env(REPEATER_CLOCK_REPAIR_SCRIPT) != ""} {
    source $::env(REPEATER_CLOCK_REPAIR_SCRIPT)
    repair_processing_clock_tree
    detailed_placement
}
estimate_parasitics -placement
repair_design
if {$design_top != "RepeaterCore"} {
    # Newly inserted clock/tie/data buffers need a global placement update.
    # Legalizing them directly can spread them far from the loads they serve.
    global_placement -incremental -density $placement_density {*}$placement_options
    detailed_placement
    estimate_parasitics -placement
    repair_timing -setup -max_iterations 100 {*}$setup_repair_options
    detailed_placement
    estimate_parasitics -placement
    repair_timing -hold -max_iterations 100
}
detailed_placement
if {$design_top != "RepeaterCore"} {
    # Hold-buffer legalization can lengthen neighboring signal wires. Repair
    # their resulting electrical loads before accepting the placement.
    estimate_parasitics -placement
    repair_design
    detailed_placement
    # Retain the real placement for path diagnosis even when routing fails.
    # Estimated wire delays here are distinct from the extracted final reports.
    estimate_parasitics -placement
    if {[info exists ::env(REPEATER_FINAL_HOLD_MARGIN)] && $::env(REPEATER_FINAL_HOLD_MARGIN) > 0} {
        # Drive-strength repair can shorten previously repaired hold paths.
        # Insert physical delay after that repair, preserving setup constraints.
        repair_timing -hold -hold_margin $::env(REPEATER_FINAL_HOLD_MARGIN) -max_iterations 100
        # Spend the placement padding reserved for late buffers. Site legality
        # and the subsequent complete PDK DRC still constrain actual geometry.
        set_placement_padding -global -left 0 -right 0
        detailed_placement
        estimate_parasitics -placement
    }
    report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/placed_timing.rpt"
    report_worst_slack -max -digits 9 > "$work/placed_setup.rpt"
    report_worst_slack -min -digits 9 > "$work/placed_hold.rpt"
    # Fail before routing if CTS violates actual electrical budgets. Restore
    # the tighter data optimization targets for the global-route repair stage.
    set_max_transition $::env(REPEATER_MAX_TRANSITION) [current_design]
    set_max_fanout 8 [current_design]
    set_max_capacitance $::env(REPEATER_MAX_CAPACITANCE) [current_design]
    report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/placed_electrical.rpt"
    set electrical_report [open "$work/placed_electrical.rpt" r]
    set electrical_text [read $electrical_report]
    close $electrical_report
    if {[string match *VIOLATED* $electrical_text]} {
        error "placed module violates slew/capacitance/fanout; inspect placed_electrical.rpt"
    }
    set_max_transition 0.3 [current_design]
    set_max_fanout $optimization_fanout [current_design]
    set_max_capacitance $optimization_capacitance [current_design]
    global_connect
    write_db "$work/placed.odb"
    write_sdc "$work/placed.sdc"
    # Global-route repair starts in a fresh process: the estimator must not
    # carry placement dirty-net state into its global-routing update mode.
    if {[info exists ::env(REPEATER_STAGE)] && $::env(REPEATER_STAGE) == "place"} {
        exit
    }
}
if {$design_top == "RepeaterCore"} {
    filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
}
global_connect
check_placement -verbose
source "$::env(REPEATER_SOURCE)/clean_empty_nets.tcl"
clean_empty_constant_nets
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
global_route -guide_file "$work/core.guide"
if {$design_top != "RepeaterCore"} {
    # Placement RC can underestimate routed detours. Repair real global-route
    # capacitance/slew and timing before committing the detailed metal routes.
    estimate_parasitics -global_routing
    repair_design -slew_margin 20 -cap_margin 10
    # Match ORFS's separate resizer/legalizer windows. repair_design and
    # repair_timing manage their own incremental parasitic updates.
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental
    estimate_parasitics -global_routing
    repair_timing -setup -max_iterations 100 {*}$setup_repair_options
    repair_timing -hold -hold_margin 0.05 -max_iterations 100
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental -guide_file "$work/core.guide"
    filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
    global_connect
    check_placement -verbose
    clean_empty_constant_nets
}
detailed_route -disable_via_gen -output_drc "$work/route_drc.rpt" -output_maze "$work/route_maze.log"
define_process_corner -ext_model_index 0 RCnom
extract_parasitics -ext_model_file "$::env(REPEATER_TOOLS)/rcx_patterns.rules" -corner_cnt 1
write_spef "$work/core.spef"
read_spef -corner slow "$work/core.spef"
read_spef -corner fast "$work/core.spef"
set_propagated_clock [all_clocks]
report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/timing.rpt"
report_worst_slack -max -digits 9 > "$work/setup.rpt"
report_worst_slack -min -digits 9 > "$work/hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/electrical.rpt"
write_def "$work/core.def"
write_db "$work/core.odb"
write_verilog -include_pwr_gnd "$work/physical.v"
write_verilog "$work/postroute.v"
write_sdc "$work/physical.sdc"
write_sdf -scene slow "$work/core.sdf"
write_cdl -include_fillers -masters "$libroot/cdl/sg13g2_stdcell.cdl" "$work/core.cdl"
