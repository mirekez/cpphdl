# Shared physical die. Setup/hold failures never gate this as-is PnR run.
set_thread_count 1
set work $::env(SHARED_WORK)
set pdk "$::env(SHARED_PDK)/ihp-sg13g2/libs.ref"
set stage $::env(SHARED_STAGE)
if {$stage == "floorplan"} {
    read_db $::env(SHARED_DIGITAL_DB)
    read_lef "$work/leaves/analog.lef"
    read_lef "$pdk/sg13g2_io/lef/sg13g2_io.lef"
} elseif {$stage == "place"} {
    read_db "$work/floorplan.odb"
} elseif {$stage == "route"} {
    read_db "$work/connected.odb"
} else {error "Unknown shared PnR stage"}
read_liberty "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib"
set macro RM_IHPSG13_2P_1024x16_c2_bm_bist
read_liberty "$pdk/sg13g2_sram/lib/${macro}_typ_1p20V_25C.lib"
set block [ord::get_db_block]
if {$stage == "floorplan"} {
    set side $::env(SHARED_CORE_SIDE)
    set die [expr {$side+640}]
    # The old logical placements are seeds only. No old module region survives.
    foreach region [$block getRegions] {odb::dbRegion_destroy $region}
    foreach inst [$block getInsts] {
        if {![[$inst getMaster] isBlock]} {$inst setPlacementStatus UNPLACED}
    }
    initialize_floorplan -die_area [list 0 0 $die $die] \
        -core_area [list 340 340 [expr {$die-340}] [expr {$die-340}]] -site CoreSite
    source "$work/devices.tcl"
    set analog_count 0
    set memories 0
    set logic_count 0
    foreach inst [$block getInsts] {
        set name [[$inst getMaster] getName]
        if {[string match analog_leaf_* $name]} {incr analog_count} \
        elseif {$name == $macro} {incr memories} \
        elseif {[string match pad_* [$inst getName]]} {} \
        else {incr logic_count}
    }
    if {$analog_count != $::env(SHARED_ANALOG_COUNT) || $memories != 8 || $logic_count != 879909} {
        error "Physical inventory changed: $analog_count analog, $logic_count logic, $memories SRAM"
    }
    puts "SHARED INVENTORY: $logic_count standard cells, $analog_count analog leaves, $memories SRAM"
    # Fixed objects are physical leaf cells; all digital modules share rows.
    # shared_cut_rows.py subtracts all fixed leaf footprints in one batch.
    make_tracks
    foreach name {VDD AVDD} {[shared_net $name] setSigType POWER}
    [shared_net VSS] setSigType GROUND
    global_connect
    write_db "$work/floorplan.odb"
    write_def "$work/floorplan.def"
    exit
}
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
if {$stage == "place"} {
    # Physical geometry and wirelength only: no timing repair or timing gate.
    place_pins -hor_layers Metal3 -ver_layers Metal2
    set_placement_padding -global -left 1 -right 1
    global_placement -density 0.70 -bin_grid_count 256
    write_db "$work/global.odb"
    write_def "$work/global.def"
    # Legalization runs in a fresh process through shared_legalize.tcl.
    exit
}
set_routing_layers -signal Metal2-TopMetal2 -clock Metal3-TopMetal2
set_global_routing_layer_adjustment Metal2-TopMetal2 0.25
global_route -allow_congestion -congestion_iterations 30 -guide_file "$work/route.guide"
write_db "$work/global_route.odb"
write_def "$work/global_route.def"
detailed_route -output_drc "$work/router_drc.rpt" -output_maze "$work/maze.log" -droute_end_iter 10
write_db "$work/routed.odb"
write_def "$work/routed.def"
