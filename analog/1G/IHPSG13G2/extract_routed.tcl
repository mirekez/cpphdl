# Recompute electrical views from an existing routed database. Qualification
# still requires native equivalence, complete physical DRC and transistor LVS.
set work $::env(REPEATER_WORK)
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count $::env(REPEATER_ROUTE_THREADS)
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$work/routed_checkpoint.odb"
set block [ord::get_db_block]
if {[$block getName] != $::env(REPEATER_TOP)} { error "routed checkpoint has the wrong top" }
set units [$block getDbUnitsPerMicron]
set die [$block getDieArea]
set expected [expr {round(double($::env(REPEATER_DIE_SIZE)) * $units)}]
if {[$die xMin] != 0 || [$die yMin] != 0 || [$die xMax] != $expected || [$die yMax] != $expected} {
    error "routed checkpoint has a different die contract"
}
read_sdc "$work/constraints.sdc"
set_propagated_clock [all_clocks]
# A checkpoint may retain the larger RC used only for routing optimization.
# Restore the original LEF before independent extraction and acceptance.
source "$::env(REPEATER_SOURCE)/routing_rc_margin.tcl"
apply_routing_capacitance_margin 1 "$libroot/lef/sg13g2_tech.lef"
set_max_transition $::env(REPEATER_MAX_TRANSITION) [current_design]
set_max_fanout 8 [current_design]
set_max_capacitance $::env(REPEATER_MAX_CAPACITANCE) [current_design]
check_placement -verbose
define_process_corner -ext_model_index 0 RCnom
extract_parasitics -ext_model_file "$::env(REPEATER_TOOLS)/rcx_patterns.rules" -corner_cnt 1
write_spef "$work/core.spef"
read_spef -corner slow "$work/core.spef"
read_spef -corner fast "$work/core.spef"
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
