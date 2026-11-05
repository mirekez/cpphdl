# Analyze an existing placement without rebuilding the netlist or placing cells.
set work $::env(UNIFIED_WORK)
set placement $::env(UNIFIED_PLACEMENT)
set refs "$::env(UNIFIED_PDK)/ihp-sg13g2/libs.ref"
set macro RM_IHPSG13_2P_1024x16_c2_bm_bist
set_thread_count 1
read_db "$placement/placed.odb"
define_corners slow fast
read_liberty -corner slow "$refs/sg13g2_stdcell/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$refs/sg13g2_stdcell/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_liberty -corner slow "$refs/sg13g2_sram/lib/${macro}_slow_1p08V_125C.lib"
read_liberty -corner fast "$refs/sg13g2_sram/lib/${macro}_fast_1p32V_m55C.lib"
read_sdc "$placement/constraints.sdc"
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
estimate_parasitics -placement
if {$::env(UNIFIED_REPAIR)} {
    set_placement_padding -global -left 1 -right 1
    set_max_fanout 8 [current_design]
    set_max_capacitance 0.08 [current_design]
    set_max_transition 0.3 [current_design]
    repair_tie_fanout -separation 0 sg13g2_tiehi/L_HI
    repair_tie_fanout -separation 0 sg13g2_tielo/L_LO
    # Tie replication changes nets outside repair_design's incremental RC
    # scope. Rebuild estimates before entering the resizer's update guard.
    estimate_parasitics -placement
    repair_design -slew_margin 10 -cap_margin 10
    write_db "$work/electrical.odb"
    detailed_placement
    check_placement -verbose
    global_connect
    write_db "$work/placed.odb"
    write_def "$work/placed.def"
    write_verilog "$work/placed.v"
    set memories 0
    set logic 0
    foreach instance [[ord::get_db_block] getInsts] {
        set master [$instance getMaster]
        if {[$master getName] == $macro} {
            incr memories
        } elseif {![$master isBlock] && [string match sg13g2_* [$master getName]]} {
            incr logic
        } else {
            error "Unexpected physical master after repair: [$master getName]"
        }
    }
    if {$memories != 8} {error "Lost frame memory during electrical repair"}
    set receipt [open "$work/inventory.json" w]
    puts $receipt "{\"movable_standard_cells\": $logic, \"sram_macros\": $memories, \"fixed_logic_modules\": 0}"
    close $receipt
    estimate_parasitics -placement
    report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/electrical.rpt"
}
check_setup -verbose > "$work/constraints.rpt"
report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/timing.rpt"
report_worst_slack -max -digits 9 > "$work/setup.rpt"
report_worst_slack -min -digits 9 > "$work/hold.rpt"
