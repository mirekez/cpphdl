# Repair selected routed signal drivers while retaining all other cell locations.
set work $::env(REPEATER_WORK)
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 2
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
if {$::env(REPEATER_ECO_STAGE) == "place"} {
    read_db "$work/input_core.odb"
    # Retain the guides from the actual checkpoint before adding buffers.
    write_guides "$work/input_routed.guide"
} else {
    read_db "$work/eco_placed.odb"
}
read_sdc "$work/input_physical.sdc"
set_propagated_clock [all_clocks]
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
add_global_connection -net VDD -inst_pattern .* -pin_pattern VDD -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern VSS -ground
set block [ord::get_db_block]
if {$::env(REPEATER_ECO_STAGE) == "place"} {
    remove_fillers
    set status [dict create]
    set positions [dict create]
    foreach inst [$block getInsts] {
        dict set status [$inst getName] [$inst getPlacementStatus]
        dict set positions [$inst getName] [$inst getLocation]
        $inst setPlacementStatus LOCKED
    }
    set affected {}
    set added {}
    set drivers [dict create]
    set report [open "$work/eco_cells.tsv" w]
    puts $report "driver\tbuffer\tmaster\tx_um\ty_um"
    set index 0
    foreach path [split $::env(REPEATER_ECO_DRIVERS) ,] {
        set separator [string last / $path]
        if {$separator < 1} { error "ECO requires a cell/output pin" }
        set inst [$block findInst [string range $path 0 [expr {$separator - 1}]]]
        if {$inst == "NULL"} { error "ECO driver instance is absent" }
        set pin [$inst findITerm [string range $path [expr {$separator + 1}] end]]
        if {$pin == "NULL" || [[$pin getMTerm] getIoType] != "OUTPUT"} {
            error "ECO target is not an output pin"
        }
        set net [$pin getNet]
        if {$net == "NULL" || [$net getSigType] != "SIGNAL"} { error "ECO target is not a signal net" }
        lappend affected [$net getName]
        set units [$block getDbUnitsPerMicron]
        set location [$inst getLocation]
        set x [expr {([lindex $location 0] + [[$inst getMaster] getWidth]) / double($units) + 0.48}]
        set y [expr {[lindex $location 1] / double($units)}]
        set buffer "eco_slew_buffer_$index"
        set new_net "eco_slew_net_$index"
        if {[$block findInst $buffer] != "NULL" || [$block findNet $new_net] != "NULL"} {
            error "ECO instance/net identity collision"
        }
        insert_buffer -buffer_cell sg13g2_buf_4 -net [$net getName] -location [list $x $y] \
            -buffer_name $buffer -net_name $new_net
        # The resizer treats requested names as prefixes and appends its
        # uniqueness counter. Read the actual instance and nets from OpenDB.
        set created {}
        foreach candidate [$block getInsts] {
            set name [$candidate getName]
            if {![dict exists $status $name] && $name ni $added} { lappend created $name }
        }
        if {[llength $created] != 1} { error "ECO did not create exactly one buffer" }
        set actual [lindex $created 0]
        lappend added $actual
        dict set drivers $actual $path
        foreach terminal [[$block findInst $actual] getITerms] {
            if {[[$terminal getMTerm] getSigType] == "SIGNAL"} {
                if {[$terminal getNet] == "NULL"} { error "ECO buffer has an unconnected signal" }
                lappend affected [[$terminal getNet] getName]
            }
        }
        incr index
    }
    detailed_placement
    foreach inst [$block getInsts] {
        set name [$inst getName]
        if {[dict exists $status $name]} {
            if {[$inst getLocation] != [dict get $positions $name]} { error "ECO moved an existing cell" }
            $inst setPlacementStatus [dict get $status $name]
        } else {
            if {$name ni $added} { error "ECO created unexpected instance $name; expected $added" }
            set location [$inst getLocation]
            puts $report "[dict get $drivers $name]\t$name\t[[$inst getMaster] getName]\t[expr {[lindex $location 0] / double($units)}]\t[expr {[lindex $location 1] / double($units)}]"
        }
    }
    close $report
    set affected [lsort -unique $affected]
    foreach name $affected {
        set net [$block findNet $name]
        if {$net == "NULL"} { error "ECO lost a modified net" }
        if {[$net getWire] != "NULL"} { odb::dbWire_destroy [$net getWire] }
    }
    set file [open "$work/eco_nets.txt" w]
    puts $file [join $affected \n]
    close $file
    global_connect
    set_nets_to_route $affected
    global_route -guide_file "$work/eco_subset.guide"
    filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
    global_connect
    check_placement -verbose
    write_db "$work/eco_placed.odb"
    exit
}
read_guides "$work/eco_merged.guide"
global_connect
# Route the corrected placement, then extract and qualify its full network.
detailed_route -disable_via_gen -output_drc "$work/route_drc.rpt" -output_maze "$work/route_maze.log"
define_process_corner -ext_model_index 0 RCnom
extract_parasitics -ext_model_file "$::env(REPEATER_TOOLS)/rcx_patterns.rules" -corner_cnt 1
write_spef "$work/core.spef"
read_spef -corner slow "$work/core.spef"
read_spef -corner fast "$work/core.spef"
report_checks -path_delay min_max -format full_clock_expanded > "$work/timing.rpt"
report_worst_slack -max > "$work/setup.rpt"
report_worst_slack -min > "$work/hold.rpt"
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
