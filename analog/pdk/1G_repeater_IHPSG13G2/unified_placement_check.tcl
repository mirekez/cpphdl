# Validate a saved database in a fresh process, without timing/repair state.
set_thread_count 1
set work $::env(UNIFIED_WORK)
read_db "$work/placed.odb"
check_placement -verbose
set logic 0
set memories 0
foreach inst [[ord::get_db_block] getInsts] {
    set master [$inst getMaster]
    if {[$master getName] == "RM_IHPSG13_2P_1024x16_c2_bm_bist"} {
        if {![$inst isFixed]} {error "SRAM lost fixed placement"}
        incr memories
    } elseif {![$master isBlock] && [string match sg13g2_* [$master getName]]} {
        if {[$inst isFixed]} {error "Fixed logic instance in common placement"}
        incr logic
    } else {
        error "Unexpected physical master: [$master getName]"
    }
}
if {$memories != 8 || $logic == 0} {error "Incomplete physical inventory"}
set receipt [open "$work/inventory.json" w]
puts $receipt "{\"movable_standard_cells\": $logic, \"sram_macros\": $memories, \"fixed_logic_modules\": 0}"
close $receipt
if {[info exists ::env(UNIFIED_EXPORT_TEXT)] && $::env(UNIFIED_EXPORT_TEXT)} {
    write_def "$work/placed.def"
    write_verilog "$work/placed.v"
}
