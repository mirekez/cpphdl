# Physical implementation of the qualified MAC-control datapath and SRAMs.
# The script writes a placement checkpoint; final routing/DRC/LVS are separate.
set work $::env(FLOW_WORK)
set pdk "$::env(FLOW_PDK)/ihp-sg13g2/libs.ref"
set macro RM_IHPSG13_2P_1024x16_c2_bm_bist
set_thread_count 1
read_lef "$work/route_tech.lef"
read_lef "$pdk/sg13g2_stdcell/lef/sg13g2_stdcell.lef"
read_lef "$pdk/sg13g2_sram/lef/$macro.lef"
define_corners slow fast
read_liberty -corner slow "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_liberty -corner slow "$pdk/sg13g2_sram/lib/${macro}_slow_1p08V_125C.lib"
read_liberty -corner fast "$pdk/sg13g2_sram/lib/${macro}_fast_1p32V_m55C.lib"
read_verilog "$work/mapped.v"
link_design FlowControlledRepeater
read_sdc "$work/constraints.sdc"
initialize_floorplan -die_area {0 0 2000 1200} -core_area {20 20 1980 1180} -site CoreSite
make_tracks
set block [ord::get_db_block]
set memories {}
foreach inst [$block getInsts] {
    if {[[$inst getMaster] getName] == $macro} { lappend memories [$inst getName] }
}
set memories [lsort $memories]
if {[llength $memories] != 8} { error "Expected eight physical frame SRAM banks" }
set dbu [$block getDbUnitsPerMicron]
set index 0
foreach name $memories {
    set inst [$block findInst $name]
    set x [expr {60 + 470 * ($index % 4)}]
    set y [expr {100 + 550 * ($index / 4)}]
    $inst setOrient R0
    $inst setLocation [expr {int($x * $dbu)}] [expr {int($y * $dbu)}]
    $inst setPlacementStatus LOCKED
    incr index
}
cut_rows -halo_width_x 10 -halo_width_y 10
add_global_connection -net VDD -inst_pattern .* -pin_pattern {^VDD$} -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern {^VSS$} -ground
add_global_connection -net VDD -inst_pattern .* -pin_pattern {^VDD!$} -power
add_global_connection -net VDD -inst_pattern .* -pin_pattern {^VDDARRAY!$} -power
add_global_connection -net VSS -inst_pattern .* -pin_pattern {^VSS!$} -ground
global_connect
set_voltage_domain -name CORE -power VDD -ground VSS
define_pdn_grid -name core -voltage_domains CORE -pins {TopMetal1}
add_pdn_stripe -grid core -layer Metal1 -width 0.44 -followpins
add_pdn_stripe -grid core -layer Metal4 -width 1.2 -pitch 20 -offset 5
add_pdn_stripe -grid core -layer Metal5 -width 2 -pitch 40 -offset 10
add_pdn_stripe -grid core -layer TopMetal1 -width 4 -pitch 80 -offset 20
add_pdn_connect -grid core -layers {Metal1 Metal4}
add_pdn_connect -grid core -layers {Metal4 Metal5}
add_pdn_connect -grid core -layers {Metal5 TopMetal1}
define_pdn_grid -macro -name memories -cells [list $macro] -orient {R0} -grid_over_pg_pins -halo 10
add_pdn_stripe -grid memories -layer Metal5 -width 2 -pitch 40 -offset 10
add_pdn_connect -grid memories -layers {Metal4 Metal5}
add_pdn_connect -grid memories -layers {Metal5 TopMetal1}
pdngen
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
place_pins -hor_layers Metal3 -ver_layers Metal2
set_placement_padding -global -left 1 -right 1
set_max_fanout 8 [current_design]
set_max_capacitance 0.08 [current_design]
set_max_transition 0.3 [current_design]
repair_design -pre_placement
global_placement -density 0.45 -timing_driven
detailed_placement
repair_tie_fanout -separation 0 sg13g2_tiehi/L_HI
repair_tie_fanout -separation 0 sg13g2_tielo/L_LO
# Finish signal placement before CTS. Moving the clock buffers in a subsequent
# global placement destroys the latency balance between SRAMs and registers.
estimate_parasitics -placement
repair_design
global_placement -incremental -density 0.45
detailed_placement
write_db "$work/pre_cts.odb"
clock_tree_synthesis -root_buf sg13g2_buf_4 -buf_list {sg13g2_buf_2 sg13g2_buf_4 sg13g2_buf_8} \
    -sink_clustering_enable -sink_clustering_size 4 -sink_clustering_max_diameter 40 \
    -distance_between_buffers 150 -branching_point_buffers_distance 10 -balance_levels \
    -obstruction_aware -repair_clock_nets
# CTS repairs long clock wires before balancing macro/register insertion delay.
# A second wire repair after balancing would add unmatched buffer stages.
detailed_placement
set_propagated_clock [all_clocks]
estimate_parasitics -placement
repair_design
detailed_placement
estimate_parasitics -placement
repair_timing -setup -setup_margin 0.2 -max_iterations 50
detailed_placement
estimate_parasitics -placement
repair_timing -hold -hold_margin 0.08 -max_iterations 50
detailed_placement
estimate_parasitics -placement
repair_design
detailed_placement
global_connect
check_placement -verbose
write_db "$work/placed.odb"
write_def "$work/placed.def"
write_verilog "$work/placed.v"
report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/placed_timing.rpt"
report_worst_slack -max -digits 9 > "$work/placed_setup.rpt"
report_worst_slack -min -digits 9 > "$work/placed_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/placed_electrical.rpt"
