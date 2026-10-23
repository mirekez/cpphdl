# Read-only placement-footprint inventory. CORE_SPACER includes decoupling and
# filler cells; it must stay separate from functional-cell area comparisons.
read_db $::env(REPEATER_DATABASE)
set block [ord::get_db_block]
set units [$block getDbUnitsPerMicron]
set die [$block getDieArea]
set groups [dict create]
foreach inst [$block getInsts] {
    set master [$inst getMaster]
    dict incr groups [list [$master getType] [$master getName]]
}
set output [open $::env(REPEATER_AREA_OUTPUT) w]
puts $output "class\tmaster\tcount\tcell_area_um2"
foreach key [lsort [dict keys $groups]] {
    lassign $key type name
    set master [[ord::get_db] findMaster $name]
    set area [expr {[$master getWidth] * double([$master getHeight]) / $units / $units}]
    puts $output "$type\t$name\t[dict get $groups $key]\t$area"
}
close $output
puts "DIE_AREA_UM2 [expr {([$die xMax] - [$die xMin]) * double([$die yMax] - [$die yMin]) / $units / $units}]"
