# OpenROAD may retain its reserved constant net after all loads were rewired
# through physical tie cells. An empty GROUND net makes TritonRoute abort.
# Remove only these reserved names with no terminals or physical wires.
proc clean_empty_constant_nets {} {
    set removed 0
    foreach net [[ord::get_db_block] getNets] {
        if {[$net getName] ni {zero_ one_}} { continue }
        if {[llength [$net getITerms]] || [llength [$net getBTerms]] ||
            [llength [$net getSWires]] || [$net getWire] ne "NULL"} { continue }
        odb::dbNet_destroy $net
        incr removed
    }
    puts "Removed $removed empty reserved constant nets; connected supply nets are preserved."
    return $removed
}
