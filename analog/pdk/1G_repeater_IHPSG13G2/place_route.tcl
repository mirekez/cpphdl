# All paths arrive through the environment, so project paths are not Tcl code.
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set work $::env(REPEATER_WORK)
set_thread_count 2
read_lef "$work/route_tech.lef"
read_lef "$libroot/lef/sg13g2_stdcell.lef"
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_verilog "$work/mapped.v"
link_design RepeaterCore
read_sdc "$::env(REPEATER_SOURCE)/constraints.sdc"
initialize_floorplan -die_area {0 0 120 120} -core_area {20 20 100 100} -site CoreSite
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
set_placement_padding -global -left 2 -right 2
global_placement -density 0.55
detailed_placement
repair_tie_fanout -separation 0 sg13g2_tiehi/L_HI
repair_tie_fanout -separation 0 sg13g2_tielo/L_LO
clock_tree_synthesis -root_buf sg13g2_buf_4 -buf_list {sg13g2_buf_2 sg13g2_buf_4 sg13g2_buf_8}
repair_clock_nets
estimate_parasitics -placement
repair_design
detailed_placement
filler_placement {sg13g2_decap_8 sg13g2_decap_4 sg13g2_fill_2 sg13g2_fill_1}
global_connect
check_placement -verbose
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
global_route -guide_file "$work/core.guide"
detailed_route -disable_via_gen -output_drc "$work/route_drc.rpt" -output_maze "$work/route_maze.log"
define_process_corner -ext_model_index 0 RCnom
extract_parasitics -ext_model_file "$::env(REPEATER_TOOLS)/rcx_patterns.rules" -corner_cnt 1
write_spef "$work/core.spef"
read_spef -corner slow "$work/core.spef"
read_spef -corner fast "$work/core.spef"
set_propagated_clock [all_clocks]
report_checks -path_delay min_max -format full_clock_expanded > "$work/timing.rpt"
report_worst_slack -max > "$work/setup.rpt"
report_worst_slack -min > "$work/hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/electrical.rpt"
write_def "$work/core.def"
write_db "$work/core.odb"
write_verilog -include_pwr_gnd "$work/physical.v"
write_verilog "$work/postroute.v"
write_sdc "$work/physical.sdc"
write_sdf -scene slow "$work/core.sdf"
write_cdl -include_fillers -masters "$libroot/cdl/sg13g2_stdcell.cdl" "$work/core.cdl"
