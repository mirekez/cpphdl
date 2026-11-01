# Route the placed SRAM datapath with both SRAM and standard-cell timing arcs.
set work $::env(FLOW_WORK)
set pdk "$::env(FLOW_PDK)/ihp-sg13g2/libs.ref"
set stage $::env(FLOW_STAGE)
set macro RM_IHPSG13_2P_1024x16_c2_bm_bist
set_thread_count 1
define_corners slow fast
foreach {corner stdsuffix memsuffix} {slow slow_1p08V_125C slow_1p08V_125C fast fast_1p32V_m40C fast_1p32V_m55C} {
    read_liberty -corner $corner "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_${stdsuffix}.lib"
    read_liberty -corner $corner "$pdk/sg13g2_sram/lib/${macro}_${memsuffix}.lib"
}
set checkpoint [dict get {global placed detail global_repaired extract routed eco routed} $stage]
read_db "$work/$checkpoint.odb"
set block [ord::get_db_block]
if {[$block getName] != "FlowControlledRepeater"} {error "wrong SRAM routing top"}
read_sdc "$work/constraints.sdc"
set_propagated_clock [all_clocks]
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_max_fanout 8 [current_design]
set_max_capacitance 0.08 [current_design]
set_max_transition 0.3 [current_design]
global_connect

if {$stage == "global"} {
    source "$::env(FLOW_COMMON)/clean_empty_nets.tcl"
    clean_empty_constant_nets
    set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
    set_global_routing_layer_adjustment Metal2-Metal5 0.30
    # Initialize DPL's macro grid before clock-net repair inserts buffers.
    detailed_placement
    global_route -guide_file "$work/core.guide"
    # Extracted long signal wires still exceeded 80 fF with a 2x optimization
    # margin (up to 110 fF). Reserve more wire load, including setup buffer
    # removal; restore the PDK model before the final hold check.
    source "$::env(FLOW_COMMON)/routing_rc_margin.tcl"
    apply_routing_capacitance_margin 3 "$pdk/sg13g2_stdcell/lef/sg13g2_tech.lef"
    set_max_transition 0.3 [current_design]
    set_max_capacitance 0.08 [current_design]
    set_max_fanout 6 [current_design]
    estimate_parasitics -global_routing
    # Limit clock-wire load without the excessive insertion delay of the
    # rejected 100 um repair. SRAM and register timing remain independently
    # checked after routing; this length is an implementation choice.
    repair_clock_nets -max_wire_length 200
    repair_design
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental
    estimate_parasitics -global_routing
    repair_timing -setup -setup_margin 0.2 -max_iterations 100
    repair_timing -hold -hold_margin 0.08 -max_iterations 50
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental
    estimate_parasitics -global_routing
    repair_design
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental -guide_file "$work/core.guide"
    report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/optimization_timing.rpt"
    apply_routing_capacitance_margin 1 "$pdk/sg13g2_stdcell/lef/sg13g2_tech.lef"
    # Nominal global RC underestimates the detailed-route loads. Preserve
    # headroom when inserting hold buffers and splitting clock branches too.
    # The final checks below retain the original 300 ps / 80 fF contract.
    set_max_transition 0.26 [current_design]
    set_max_capacitance 0.06 [current_design]
    estimate_parasitics -global_routing
    # Hold must be checked with original RC: an enlarged wire model can hide
    # the shortest real paths into SRAM address/data pins.
    repair_timing -hold -hold_margin 0.12 -max_iterations 100
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental -guide_file "$work/core.guide"
    estimate_parasitics -global_routing
    repair_antennas sg13g2_antennanp -diode_only -iterations 5 -ratio_margin 20
    # Diodes add real load/fanout. Refresh RC annotations after antenna edits
    # before entering the resizer again, then repair their additional load.
    estimate_parasitics -placement
    estimate_parasitics -global_routing
    repair_design
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental -guide_file "$work/core.guide"
    estimate_parasitics -global_routing
    repair_antennas sg13g2_antennanp -diode_only -iterations 5 -ratio_margin 20
    estimate_parasitics -placement
    estimate_parasitics -global_routing
    source [file join [file dirname [info script]] flow_control_clock_loads.tcl]
    for {set pass 0} {$pass < 3} {incr pass} {
        set report "$work/clock_loads_$pass.rpt"
        report_check_types -max_capacitance -violators > $report
        if {[split_flow_clock_loads $report $pass] == 0} {break}
        global_route -start_incremental
        detailed_placement
        global_route -end_incremental
        estimate_parasitics -global_routing
        repair_timing -hold -hold_margin 0.12 -max_iterations 100
        global_route -start_incremental
        detailed_placement
        global_route -end_incremental
        estimate_parasitics -global_routing
        repair_design
        global_route -start_incremental
        detailed_placement
        global_route -end_incremental -guide_file "$work/core.guide"
        estimate_parasitics -global_routing
        repair_antennas sg13g2_antennanp -diode_only -iterations 5 -ratio_margin 20
        estimate_parasitics -placement
        estimate_parasitics -global_routing
    }
    set_max_transition 0.3 [current_design]
    set_max_capacitance 0.08 [current_design]
    set_max_fanout 8 [current_design]
    check_antennas -report_file "$work/global_antenna.rpt"
    filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
    global_connect
    check_placement -verbose
    write_db "$work/global_repaired.odb"
    write_sdc "$work/global_repaired.sdc"
    report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/global_timing.rpt"
    report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
    report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
    report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/global_electrical.rpt"
    set f [open "$work/global_electrical.rpt" r]
    set electrical [read $f]
    close $f
    if {[sta::worst_slack_cmd max] < 0 || [sta::worst_slack_cmd min] < 0 ||
        [string first "VIOLATED" $electrical] >= 0} {
        error "global SRAM/control timing or electrical limits fail; inspect retained reports"
    }
    exit
}
if {$stage == "eco"} {
    # Repair actual extracted loads, then route and extract the changed design
    # again. Estimated RC alone missed a long signal and a loaded CTS branch.
    remove_fillers
    # This pass reroutes the whole block. Old detailed wires can obstruct pin
    # access after legalization moves a cell; retaining them is only valid for
    # a separately constrained incremental ECO with fixed original locations.
    foreach net [$block getNets] {
        if {[$net getSigType] in {SIGNAL CLOCK} && [$net getWire] != "NULL"} {
            odb::dbWire_destroy [$net getWire]
        }
    }
    set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
    set_global_routing_layer_adjustment Metal2-Metal5 0.30
    global_route
    read_spef -corner slow "$work/core.spef"
    read_spef -corner fast "$work/core.spef"
    report_check_types -max_capacitance -violators > "$work/eco_loads.rpt"
    source [file join [file dirname [info script]] flow_control_signal_loads.tcl]
    split_flow_signal_loads "$work/eco_loads.rpt" extracted
    source [file join [file dirname [info script]] flow_control_clock_loads.tcl]
    split_flow_clock_loads "$work/eco_loads.rpt" extracted
    detailed_placement
    # Rebuild guides after topology edits; the old extracted RC is no longer
    # valid for the inserted nets. Final acceptance uses a fresh extraction.
    global_route -guide_file "$work/core.guide"
    estimate_parasitics -global_routing
    repair_timing -hold -hold_margin 0.12 -max_iterations 100
    global_route -start_incremental
    detailed_placement
    global_route -end_incremental -guide_file "$work/core.guide"
    estimate_parasitics -global_routing
    repair_antennas sg13g2_antennanp -diode_only -iterations 5 -ratio_margin 20
    detailed_placement
    global_route -guide_file "$work/core.guide"
    estimate_parasitics -global_routing
    check_antennas -report_file "$work/global_antenna.rpt"
    filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
    global_connect
    check_placement -verbose
    write_db "$work/global_repaired.odb"
    write_sdc "$work/global_repaired.sdc"
    exit
}
if {$stage == "detail"} {
    check_placement -verbose
    detailed_route -disable_via_gen -output_drc "$work/route_drc.rpt" -output_maze "$work/route_maze.log"
    write_db "$work/routed.odb"
    write_def "$work/core.def"
    exit
}
define_process_corner -ext_model_index 0 RCnom
extract_parasitics -ext_model_file "$::env(FLOW_TOOLS)/rcx_patterns.rules" -corner_cnt 1
write_spef "$work/core.spef"
read_spef -corner slow "$work/core.spef"
read_spef -corner fast "$work/core.spef"
report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/timing.rpt"
report_worst_slack -max -digits 9 > "$work/setup.rpt"
report_worst_slack -min -digits 9 > "$work/hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/electrical.rpt"
write_def "$work/core.def"
write_verilog "$work/postroute.v"
write_verilog -include_pwr_gnd "$work/physical.v"
write_sdc "$work/physical.sdc"
write_sdf -scene slow "$work/core.sdf"
write_cdl -include_fillers -masters [list "$pdk/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl" "$pdk/sg13g2_sram/cdl/$macro.cdl"] "$work/core.cdl"
