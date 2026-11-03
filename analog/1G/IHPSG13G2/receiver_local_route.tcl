# Legalize the selected sized receiver, rebuild routes, then extract in fresh
# processes. The original SPEF is never applied to the changed topology.
set work $::env(REPEATER_WORK)
set stage $::env(RECEIVER_ECO_STAGE)
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
if {$stage != "detail"} {
    define_corners slow fast
    read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
    read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
}
if {$stage == "global"} {
    read_db "$::env(RECEIVER_SEED)"
} elseif {$stage == "detail"} {
    read_db "$work/global.odb"
} elseif {$stage == "extract"} {
    read_db "$work/routed.odb"
} else { error "unknown receiver ECO stage" }
set block [ord::get_db_block]
if {[$block getName] != "PCSReceiver"} { error "receiver-only ECO" }
if {$stage != "detail"} {
    read_sdc "$::env(RECEIVER_SDC)"
    set_propagated_clock [all_clocks]
    set_max_transition 0.5 [current_design]
    set_max_fanout 8 [current_design]
    set_max_capacitance 0.1 [current_design]
    add_global_connection -net VDD -inst_pattern .* -pin_pattern VDD -power
    add_global_connection -net VSS -inst_pattern .* -pin_pattern VSS -ground
}
if {$stage == "global"} {
    write_guides "$work/original.guide"
    read_guides "$work/original.guide"
    write_verilog "$work/before.v"
    source "$::env(RECEIVER_SOURCE)/receiver_local_unbuffer.tcl"
    set selected [split $::env(RECEIVER_BUFFERS) ,]
    puts "CPPHDL: removed [remove_receiver_local_buffers $selected 60 "$work/removed.csv"] local data buffers"
    write_verilog "$work/unbuffered.v"
    remove_fillers
    write_verilog "$work/unfilled.v"
    # Resized cells need legalization even though their signal topology was
    # unchanged. Rebuild all signal wires after legalization and removal.
    detailed_placement
    check_placement -verbose
    foreach net [$block getNets] {
        if {![$net isSpecial] && [$net getWire] != "NULL"} { odb::dbWire_destroy [$net getWire] }
    }
    set_wire_rc -signal -layer Metal3
    set_wire_rc -clock -layer Metal5
    set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
    set_global_routing_layer_adjustment Metal2-Metal5 0.30
    global_connect
    global_route -guide_file "$work/global.guide"
    estimate_parasitics -global_routing
    report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
    report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
    write_db "$work/global.odb"
    write_verilog "$work/global.v"
} elseif {$stage == "detail"} {
    check_placement -verbose
    set route_options {}
    if {[info exists ::env(RECEIVER_ROUTE_PROBE)] && $::env(RECEIVER_ROUTE_PROBE) == "1"} {
        # A separate diagnostic driver uses this early route to measure actual
        # wires before spending hours on closure. It never qualifies a layout.
        set route_options {-droute_end_iter 1}
    }
    detailed_route -disable_via_gen -output_drc "$work/route_drc.rpt" -output_maze "$work/route_maze.log" {*}$route_options
    write_db "$work/routed.odb"
} else {
    filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
    global_connect
    check_placement -verbose
    source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
    apply_routing_capacitance_margin 1 "$libroot/lef/sg13g2_tech.lef"
    define_process_corner -ext_model_index 0 RCnom
    extract_parasitics -ext_model_file "$::env(REPEATER_TOOLS)/rcx_patterns.rules" -corner_cnt 1
    write_spef "$work/core.spef"
    foreach corner {slow fast} { read_spef -corner $corner "$work/core.spef" }
    report_worst_slack -max -digits 9 > "$work/setup.rpt"
    report_worst_slack -min -digits 9 > "$work/hold.rpt"
    report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
        -fields {slew cap fanout input_pins} -digits 9 > "$work/paths.rpt"
    report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/electrical.rpt"
    write_db "$work/core.odb"
    write_def "$work/core.def"
    write_verilog "$work/postroute.v"
    write_verilog -include_pwr_gnd "$work/physical.v"
    write_sdc "$work/physical.sdc"
    write_cdl -include_fillers -masters "$libroot/cdl/sg13g2_stdcell.cdl" "$work/core.cdl"
}
exit
