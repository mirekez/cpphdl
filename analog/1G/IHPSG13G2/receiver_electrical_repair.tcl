# Use the measured routed RC to repair actual overloaded receiver nets.
# Changed topology requires new routing/extraction; this stage is a candidate.
set work $::env(REPEATER_WORK)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$::env(RECEIVER_SEED)"
if {[[ord::get_db_block] getName] != "PCSReceiver"} { error "receiver-only electrical repair" }
read_sdc "$::env(RECEIVER_SDC)"
set_propagated_clock [all_clocks]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
write_guides "$work/restored.guide"
read_guides "$work/restored.guide"
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
write_verilog "$work/before.v"
remove_fillers
write_verilog "$work/unfilled.v"
foreach corner {slow fast} { read_spef -corner $corner "$::env(RECEIVER_SPEF)" }
report_worst_slack -max -digits 9 > "$work/before_setup.rpt"
report_worst_slack -min -digits 9 > "$work/before_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/before_electrical.rpt"
# Optimize below the acceptance limits: 0.30 ns slew and 0.07 pF load.
# Routed acceptance still uses 0.50 ns and 0.10 pF after fresh extraction.
repair_design -slew_margin 40 -cap_margin 30 -verbose
write_verilog "$work/repaired.v"
# The old SPEF is invalid after buffer insertion. Rebuild placement and all
# signal routing estimates before inspecting the candidate timing.
detailed_placement
check_placement -verbose
set block [ord::get_db_block]
foreach net [$block getNets] {
    if {![$net isSpecial] && [$net getWire] != "NULL"} { odb::dbWire_destroy [$net getWire] }
}
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
global_route -guide_file "$work/global.guide"
estimate_parasitics -global_routing
report_worst_slack -max -digits 9 > "$work/global_setup.rpt"
report_worst_slack -min -digits 9 > "$work/global_hold.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/global_electrical.rpt"
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \
    -fields {slew cap fanout input_pins} -digits 9 > "$work/global_paths.rpt"
write_verilog "$work/global.v"
write_db "$work/global.odb"
exit
