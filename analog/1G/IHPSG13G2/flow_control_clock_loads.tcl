# Split overloaded clock branches into two local, non-inverting branches.
# Wire-length repair alone cannot reduce the input capacitance of seven or
# eight large leaf buffers driven by one parent. All limits remain unchanged.
proc split_flow_clock_loads {report pass} {
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
        if {$net == "NULL" || [$net getSigType] != "CLOCK" ||
            [dict exists $seen $net_name]} {continue}
        dict set seen $net_name 1
        set loads {}
        set supported 1
        foreach term [$net getITerms] {
            if {[$term getIoType] != "INPUT"} {continue}
            set inst [$term getInst]
            set master [[$inst getMaster] getName]
            set port [[$term getMTerm] getName]
            # CTS may attach a register directly beside its buffer leaves.
            # Its clock input can move with the branch; data/reset inputs cannot.
            set clock_register [expr {$master == "sg13g2_dfrbpq_1" && $port == "CLK"}]
            # An inverter can terminate a CTS balancing branch. Inserting a
            # non-inverting buffer before A preserves that leaf's polarity.
            if {![regexp {^sg13g2_((buf|inv)_(1|2|4|8|16)|antennanp)$} $master] &&
                !$clock_register} {
                set supported 0
                break
            }
            set point [$inst getLocation]
            lappend loads [list "[$inst getName]/[[$term getMTerm] getName]" \
                [expr {double([lindex $point 0]) / $dbu}] \
                [expr {double([lindex $point 1]) / $dbu}]]
        }
        if {!$supported || [llength $loads] < 4} {continue}
        set xs [lsort -real -index 1 $loads]
        set ys [lsort -real -index 2 $loads]
        set dx [expr {[lindex $xs end 1] - [lindex $xs 0 1]}]
        set dy [expr {[lindex $ys end 2] - [lindex $ys 0 2]}]
        set ordered [expr {$dx > $dy ? $xs : $ys}]
        set middle [expr {[llength $ordered] / 2}]
        foreach group [list [lrange $ordered 0 [expr {$middle - 1}]] \
                            [lrange $ordered $middle end]] {
            set pins {}
            set x 0.0
            set y 0.0
            foreach load $group {
                lappend pins [lindex $load 0]
                set x [expr {$x + [lindex $load 1]}]
                set y [expr {$y + [lindex $load 2]}]
            }
            set x [expr {$x / [llength $group]}]
            set y [expr {$y / [llength $group]}]
            set name flow_clock_split_${pass}_$inserted
            insert_buffer -buffer_cell sg13g2_buf_4 -load_pins $pins \
                -location [list $x $y] -buffer_name $name -net_name ${name}_net
            # OpenROAD uniquifies the requested base name. Follow the actual
            # rewired load instead of guessing that generated net's spelling.
            set added [get_name [get_nets -of_objects [get_pins [lindex $pins 0]]]]
            if {$added == $net_name} {error "clock branch insertion did not rewire its load"}
            [$block findNet $added] setSigType CLOCK
            incr inserted
        }
        puts "Split overloaded clock branch $net_name into two local branches"
    }
    return $inserted
}
