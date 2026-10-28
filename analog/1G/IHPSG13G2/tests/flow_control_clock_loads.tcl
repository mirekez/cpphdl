# Exercise branch rewiring on real IHP cells, including a direct register load.
set work $::env(CLOCK_SPLIT_WORK)
set library "$::env(CLOCK_SPLIT_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
read_lef "$library/lef/sg13g2_tech.lef"
read_lef "$library/lef/sg13g2_stdcell.lef"
read_liberty "$library/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_verilog "$work/fixture.v"
link_design ClockSplitFixture
initialize_floorplan -site CoreSite -die_area {0 0 400 200} -core_area {10 10 390 190}
set block [ord::get_db_block]
set position 0
foreach inst [$block getInsts] {
    $inst setLocation [expr {20000 + 10000 * ($position % 12)}] \
                      [expr {20000 + 20000 * ($position / 12)}]
    $inst setPlacementStatus PLACED
    incr position
}
[$block findInst signal_source] setLocation 20000 150000
[$block findInst signal_sink] setLocation 350000 150000
detailed_placement
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
foreach net {mixed_clock buffer_clock reset_branch} {
    [$block findNet $net] setSigType CLOCK
}
# A lower fixture limit makes these small branches overload reproducibly.
# The production design's 80 fF limit is unchanged.
set_max_capacitance 0.02 [current_design]
estimate_parasitics -placement
report_check_types -max_capacitance -violators > "$work/loads.rpt"
set file [open "$work/loads.rpt" r]
set violations [read $file]
close $file
foreach parent {parent0 parent1 parent2 parent3 signal_source} {
    if {![regexp -line "^${parent}/X\\s+.*VIOLATED" $violations]} {
        error "fixture did not overload its intended branch: $parent"
    }
}
set before [dict create]
foreach inst [$block getInsts] {
    foreach term [$inst getITerms] {
        if {[$term getIoType] == "INPUT" && [$term getNet] != "NULL"} {
            dict set before "[$inst getName]/[[$term getMTerm] getName]" [[$term getNet] getName]
        }
    }
}
set initial_count [llength [$block getInsts]]
source $::env(CLOCK_SPLIT_HELPER)
set inserted [split_flow_clock_loads "$work/loads.rpt" 0]
if {$inserted != 4 || [llength [$block getInsts]] != $initial_count + 4} {
    error "expected exactly two repaired clock branches, got $inserted buffers"
}
set groups [dict create]
set moved 0
dict for {pin original} $before {
    set actual [get_name [get_nets -of_objects [get_pins $pin]]]
    if {$original ni {mixed_clock buffer_clock}} {
        if {$actual != $original} {error "changed data/reset or unrelated connection: $pin"}
        continue
    }
    if {$actual == $original} {error "clock load was omitted: $pin"}
    set net [$block findNet $actual]
    if {[$net getSigType] != "CLOCK"} {error "new branch lost clock classification"}
    set drivers {}
    foreach term [$net getITerms] {
        if {[$term getIoType] == "OUTPUT"} {lappend drivers [$term getInst]}
    }
    if {[llength $drivers] != 1} {error "branch has missing or multiple drivers"}
    set buffer [lindex $drivers 0]
    if {[[$buffer getMaster] getName] != "sg13g2_buf_4" ||
        [[[$buffer findITerm A] getNet] getName] != $original} {
        error "branch does not preserve its original non-inverting clock source"
    }
    dict lappend groups $original $actual
    incr moved
}
if {$moved != 12} {error "expected all twelve original clock loads to move"}
if {[[[$block findInst clock_load] getMaster] getName] != "sg13g2_inv_1" ||
    [[[$block findInst clock_load] findITerm Y] getNet] != [$block findNet inverted]} {
    error "clock load inversion or output connection changed"
}
dict for {original branches} $groups {
    if {[llength [lsort -unique $branches]] != 2} {error "clock loads did not split into two branches"}
}
set file [open "$work/empty.rpt" w]
close $file
if {[split_flow_clock_loads "$work/empty.rpt" 1] != 0} {error "empty report changed topology"}
set connectivity [dict create]
foreach inst [$block getInsts] {
    foreach term [$inst getITerms] {
        if {[$term getNet] != "NULL"} {
            dict set connectivity "[$inst getName]/[[$term getMTerm] getName]" [[$term getNet] getName]
        }
    }
}
source $::env(SIGNAL_SPLIT_HELPER)
if {[split_flow_signal_loads "$work/loads.rpt" 0] != 1} {error "signal wire was not split exactly once"}
dict for {pin original} $connectivity {
    set actual [get_name [get_nets -of_objects [get_pins $pin]]]
    if {$pin == "signal_sink/A"} {
        if {$actual == $original} {error "signal load stayed on overloaded net"}
        set buffer ""
        foreach term [[$block findNet $actual] getITerms] {
            if {[$term getIoType] == "OUTPUT"} {set buffer [$term getInst]}
        }
        if {$buffer == "" || [[$buffer getMaster] getName] != "sg13g2_buf_4" ||
            [[[$buffer findITerm A] getNet] getName] != $original} {
            error "signal segment lost its non-inverting source"
        }
        set point [$buffer getLocation]
        set source [[$block findInst signal_source] getLocation]
        set sink [[$block findInst signal_sink] getLocation]
        if {[lindex $point 0] <= [lindex $source 0] || [lindex $point 0] >= [lindex $sink 0]} {
            error "signal buffer is not between its source and sink"
        }
    } elseif {$actual != $original} {error "signal repair changed an unrelated connection: $pin"}
}
if {[split_flow_signal_loads "$work/empty.rpt" 1] != 0} {error "empty signal report changed topology"}
puts "PASS extracted-load repair: twelve clock loads, preserved inversion/reset/data, split signal wire"
exit
