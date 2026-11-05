# Repair a bounded list of real driver nets and save every completed repair.
# A saved repair.odb is intermediate until detailed placement succeeds.
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
set_placement_padding -global -left 1 -right 1
set_max_fanout 8 [current_design]
set_max_capacitance 0.08 [current_design]
set_max_transition 0.3 [current_design]
estimate_parasitics -placement
set block [ord::get_db_block]
set input [open "$work/drivers.txt" r]
set drivers [split [string trim [read $input]] "\n"]
close $input
set log [open "$work/nets.tsv" w]
puts $log "driver\tbefore_terminals\tafter_terminals\tseconds"
foreach driver $drivers {
    set inst [$block findInst $driver]
    if {$inst == "NULL"} {error "Missing selected driver: $driver"}
    set outputs {}
    foreach pin [$inst getITerms] {
        if {[$pin getIoType] == "OUTPUT" && [$pin getSigType] == "SIGNAL"} {
            lappend outputs $pin
        }
    }
    if {[llength $outputs] != 1} {error "Expected one signal output: $driver"}
    set pin [lindex $outputs 0]
    set net [$pin getNet]
    if {$net == "NULL" || [$net getSigType] != "SIGNAL"} {error "Invalid repair net: $driver"}
    set before [$net getITermCount]
    set start [clock seconds]
    puts "Repairing $driver with $before terminals"
    flush stdout
    rsz::repair_net_cmd [sta::find_net [$net getName]] 0 10 10
    puts $log "$driver\t$before\t[[$pin getNet] getITermCount]\t[expr {[clock seconds] - $start}]"
    flush $log
    # Retain the preceding completed step if this write or later work fails.
    write_db "$work/repair.next.odb"
    file rename -force "$work/repair.next.odb" "$work/repair.odb"
}
close $log
detailed_placement
check_placement -verbose
global_connect
write_db "$work/placed.odb"
# Inventory/export and timing run in fresh processes. Keeping the resizer,
# placement grid and timing graph together exceeded this host's memory limit.
