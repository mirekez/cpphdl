# Divide an extracted, overloaded point-to-point wire into two driven segments.
# Resizing its driver cannot reduce the wire capacitance seen at that driver.
proc split_flow_signal_loads {report pass} {
    set file [open $report r]
    set text [read $file]
    close $file
    set block [ord::get_db_block]
    set dbu [$block getDbUnitsPerMicron]
    set seen [dict create]
    set inserted 0
    foreach line [split $text "\n"] {
        if {![regexp {^(\S+)\s+.*VIOLATED} $line -> pin_name]} {continue}
        set pin [get_pins -quiet $pin_name]
        if {$pin == "" || [get_property $pin direction] != "output"} {continue}
        set net_name [get_name [get_nets -of_objects $pin]]
        set net [$block findNet $net_name]
        if {$net == "NULL" || [$net getSigType] != "SIGNAL" ||
            [dict exists $seen $net_name] || [llength [$net getBTerms]] != 0} {continue}
        dict set seen $net_name 1
        set loads {}
        set drivers {}
        foreach term [$net getITerms] {
            # Antenna diodes protect the existing wire; they are not logic
            # sinks. Keep them on that segment when buffering its sole
            # functional load. The rerouted branches get a fresh antenna check.
            if {[$term getIoType] == "INPUT" &&
                [[[$term getInst] getMaster] getName] != "sg13g2_antennanp"} {
                lappend loads $term
            }
            if {[$term getIoType] == "OUTPUT"} {lappend drivers $term}
        }
        if {[llength $loads] != 1 || [llength $drivers] != 1} {continue}
        set load [lindex $loads 0]
        set driver [lindex $drivers 0]
        set a [[$driver getInst] getLocation]
        set b [[$load getInst] getLocation]
        set distance [expr {abs([lindex $a 0] - [lindex $b 0]) + abs([lindex $a 1] - [lindex $b 1])}]
        if {$distance < 20 * $dbu} {continue}
        set point [list [expr {double([lindex $a 0] + [lindex $b 0]) / (2 * $dbu)}] \
                        [expr {double([lindex $a 1] + [lindex $b 1]) / (2 * $dbu)}]]
        set load_name "[[$load getInst] getName]/[[$load getMTerm] getName]"
        set name flow_signal_split_${pass}_$inserted
        insert_buffer -buffer_cell sg13g2_buf_4 -load_pins [list $load_name] \
            -location $point -buffer_name $name -net_name ${name}_net
        if {[get_name [get_nets -of_objects [get_pins $load_name]]] == $net_name} {
            error "signal buffer did not rewire its load"
        }
        incr inserted
        puts "Split extracted point-to-point signal wire $net_name"
    }
    return $inserted
}
