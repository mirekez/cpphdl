# Route the shared die without timing gates, in separately resumable stages.
set_thread_count 1
set work $::env(SHARED_WORK)
set pdk "$::env(SHARED_PDK)/ihp-sg13g2/libs.ref"
set stage $::env(SHARED_ROUTE_STAGE)
if {$stage == "global"} {
    read_db "$work/buffered.odb"
} elseif {$stage == "detail"} {
    read_db "$work/global_route.odb"
} else {error "Expected global or detail routing stage"}
read_liberty "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib"
read_liberty "$pdk/sg13g2_sram/lib/RM_IHPSG13_2P_1024x16_c2_bm_bist_typ_1p20V_25C.lib"
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_routing_layers -signal Metal2-TopMetal2 -clock Metal3-TopMetal2
if {$stage == "global"} {
    source [file join [file dirname [info script]] shared_route_grid.tcl]
    shared_coarse_grid
    set_global_routing_layer_adjustment Metal2-TopMetal2 0.25
    set engine {}
    if {[info exists ::env(SHARED_ROUTE_ENGINE)] && $::env(SHARED_ROUTE_ENGINE) == "cugr"} {set engine {-use_cugr}}
    global_route {*}$engine -verbose -critical_nets_percentage 0 -allow_congestion \
        -congestion_iterations 10 -congestion_report_file "$work/congestion.rpt" \
        -congestion_report_iter_step 1 -guide_file "$work/route.guide"
    shared_restore_tracks
    write_db "$work/global_route.odb"
    write_def "$work/global_route.def"
} else {
    read_guides "$work/route.guide"
    detailed_route -output_drc "$work/router_drc.rpt" -output_maze "$work/maze.log" -droute_end_iter 10
    write_db "$work/routed.odb"
    write_def "$work/routed.def"
}
