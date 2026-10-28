# Route the repaired network in a fresh process. STA's global-route search and
# parasitic caches are deliberately absent while the detailed router is resident.
set work $::env(REPEATER_WORK)
set_thread_count $::env(REPEATER_ROUTE_THREADS)
read_db "$work/global_repaired.odb"
set block [ord::get_db_block]
if {[$block getName] != $::env(REPEATER_TOP)} {error "global checkpoint has the wrong top"}
set units [$block getDbUnitsPerMicron]
set die [$block getDieArea]
set expected [expr {round(double($::env(REPEATER_DIE_SIZE)) * $units)}]
if {[$die xMin] != 0 || [$die yMin] != 0 || [$die xMax] != $expected || [$die yMax] != $expected} {
    error "global checkpoint has a different die contract"
}
set guided 0
foreach net [$block getNets] {
    if {[llength [$net getGuides]] > 0} {incr guided}
}
if {$guided == 0} {error "global checkpoint has no routing guides"}
check_placement -verbose
file mkdir "$work/route_snapshots"
set_debug_level DRT snapshot 1
detailed_route_debug -snapshot_dir "$work/route_snapshots"
detailed_route -disable_via_gen -output_drc "$work/route_drc.rpt" -output_maze "$work/route_maze.log"
write_db "$work/core.odb"
write_def "$work/core.def"
# Extraction and timing run in another process, followed by independent
# functional comparison and full physical checks. This is only a routed DB.
