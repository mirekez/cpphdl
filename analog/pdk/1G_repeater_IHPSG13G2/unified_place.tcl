# One movable standard-cell population, with only SRAM kept as hard macros.
# This is a placement checkpoint, not routed/full-chip/timing qualification.
set work $::env(UNIFIED_WORK)
set pdk "$::env(UNIFIED_PDK)/ihp-sg13g2/libs.ref"
set macro RM_IHPSG13_2P_1024x16_c2_bm_bist
set_thread_count 1
set resume [info exists ::env(UNIFIED_RESUME_DB)]
if {$resume} {
    read_db $::env(UNIFIED_RESUME_DB)
} else {
    read_lef "$work/route_tech.lef"
    read_lef "$pdk/sg13g2_stdcell/lef/sg13g2_stdcell.lef"
    read_lef "$pdk/sg13g2_sram/lef/$macro.lef"
}
define_corners slow fast
read_liberty -corner slow "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_liberty -corner slow "$pdk/sg13g2_sram/lib/${macro}_slow_1p08V_125C.lib"
read_liberty -corner fast "$pdk/sg13g2_sram/lib/${macro}_fast_1p32V_m55C.lib"
if {!$resume} {
    read_verilog $::env(UNIFIED_NETLIST)
    link_design RepeaterDigital
}
read_sdc "$work/constraints.sdc"
set block [ord::get_db_block]
set dbu [$block getDbUnitsPerMicron]
set memories {}
set logic_area 0.0
set memory_area 0.0
set memory_width 0.0
set memory_height 0.0
set movable 0
foreach inst [$block getInsts] {
    set master [$inst getMaster]
    set name [$master getName]
    set w [expr {double([$master getWidth]) / $dbu}]
    set h [expr {double([$master getHeight]) / $dbu}]
    if {$name == $macro} {
        lappend memories [$inst getName]
        set memory_width $w
        set memory_height $h
        set memory_area [expr {$memory_area + $w * $h}]
    } else {
        if {[$master isBlock] || ![string match sg13g2_* $name]} {
            error "Whole logical module or unexpected macro in placement: $name"
        }
        $inst setPlacementStatus UNPLACED
        set logic_area [expr {$logic_area + $w * $h}]
        incr movable
    }
}
set memories [lsort $memories]
if {[llength $memories] != 8 || $movable == 0} { error "Missing SRAM or movable logic" }
if {!$resume} {
# Derive a new common floorplan from actual cell area. Old block dimensions,
# coordinates, fences and empty margins are deliberately not imported.
set margin 40
set gap 30
set minimum_width [expr {4 * $memory_width + 5 * $gap + 2 * $margin}]
set minimum_height [expr {2 * $memory_height + 3 * $gap + 2 * $margin}]
set side [expr {ceil(sqrt($logic_area / 0.42 + $memory_area) / 20.0) * 20 + 2 * $margin}]
set side [expr {max($side, $minimum_width, $minimum_height)}]
initialize_floorplan -die_area [list 0 0 $side $side] \
    -core_area [list $margin $margin [expr {$side - $margin}] [expr {$side - $margin}]] -site CoreSite
make_tracks
# SRAM is a physical leaf. Two rows near the center leave common logic rows
# around and between the memories; no logic-module fence is created.
set x0 [expr {($side - 4 * $memory_width - 3 * $gap) / 2}]
set y0 [expr {($side - 2 * $memory_height - $gap) / 2}]
set index 0
foreach name $memories {
    set inst [$block findInst $name]
    $inst setOrient R0
    $inst setLocation [expr {int(($x0 + ($index % 4) * ($memory_width + $gap)) * $dbu)}] \
                      [expr {int(($y0 + ($index / 4) * ($memory_height + $gap)) * $dbu)}]
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
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
place_pins -hor_layers Metal3 -ver_layers Metal2
write_db "$work/floorplan.odb"
write_def "$work/floorplan.def"
set summary [open "$work/inventory.json" w]
puts $summary "{\"movable_standard_cells\": $movable, \"sram_macros\": 8, \"fixed_logic_modules\": 0, \"die_side_um\": $side, \"logic_area_um2\": $logic_area}"
close $summary
}
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_placement_padding -global -left 1 -right 1
set_max_fanout 8 [current_design]
set_max_capacitance 0.08 [current_design]
set_max_transition 0.3 [current_design]
# Do not gate placement on timing success. Retain the actual failed timing
# reports as well as the placed database for subsequent clock/routing repair.
set placement_options {-timing_driven -routability_driven}
if {$::env(UNIFIED_GEOMETRIC_ONLY)} {
    set placement_options {}
}
if {$::env(UNIFIED_BIN_GRID) > 0} {
    lappend placement_options -bin_grid_count $::env(UNIFIED_BIN_GRID)
}
global_placement -density 0.55 {*}$placement_options
write_db "$work/global.odb"
detailed_placement
check_placement -verbose
global_connect
write_db "$work/placed.odb"
write_def "$work/placed.def"
write_verilog "$work/placed.v"
if {!$::env(UNIFIED_GEOMETRIC_ONLY)} {
    estimate_parasitics -placement
    report_checks -path_delay min_max -format full_clock_expanded -digits 9 > "$work/timing.rpt"
    report_worst_slack -max -digits 9 > "$work/setup.rpt"
    report_worst_slack -min -digits 9 > "$work/hold.rpt"
}
