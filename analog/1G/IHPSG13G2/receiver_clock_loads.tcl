# Isolate explicit overloaded CTS branches at wire midpoints. Only clock buffer
# and register-clock loads are allowed; all original endpoints remain connected.
proc receiver_clock_loads {names journal prefix} {
    set block [ord::get_db_block]
    if {[$block getName] != "PCSReceiver"} {error "receiver clock checkpoint required"}
    if {[llength $names] > 16 || [llength $names] != [llength [lsort -unique $names]]} {
        error "at most 16 unique clock drivers required"
    }
    if {![regexp {^[a-zA-Z][a-zA-Z0-9_]*$} $prefix]} {error "invalid buffer prefix"}
    set clocks [dict create]
    foreach net [sta::find_all_clk_nets] {dict set clocks [$net getName] 1}
    set units [$block getDbUnitsPerMicron]
    set plans {}
    # Validate the complete selection before changing any connectivity.
    foreach name $names {
        set pin [get_pins -quiet $name]
        if {$pin == "" || [get_property $pin direction] != "output"} {error "clock output required: $name"}
        set net_name [get_name [get_nets -of_objects $pin]]
        set net [$block findNet $net_name]
        if {$net == "NULL" || ![dict exists $clocks $net_name] ||
            [$net isDoNotTouch] || [$net isSpecial] || [llength [$net getBTerms]]} {
            error "unprotected internal clock net required: $name"
        }
        set source_name [get_name [get_cells -of_objects $pin]]
        set source [$block findInst $source_name]
        if {$source == "NULL" || [$source isDoNotTouch] ||
            ![regexp {^sg13g2_buf_(1|2|4|8|16)$} [[$source getMaster] getName]]} {
            error "IHP clock buffer driver required: $name"
        }
        set origin [$source getLocation]
        set loads {}
        foreach term [$net getITerms] {
            if {[$term getIoType] == "OUTPUT"} {
                if {[$term getInst] != $source} {error "multiple clock drivers"}
                continue
            }
            set inst [$term getInst]
            set master [[$inst getMaster] getName]
            set port [[$term getMTerm] getName]
            if {[$term getIoType] != "INPUT" || [$inst isDoNotTouch] ||
                !(([regexp {^sg13g2_(buf|inv)_(1|2|4|8|16)$} $master] && $port == "A") ||
                  ([regexp {^sg13g2_dfrbpq_(1|2)$} $master] && $port == "CLK"))} {
                error "unsupported clock load [$inst getName]/$port"
            }
            set point [$inst getLocation]
            set x [expr {double([lindex $point 0] + [lindex $origin 0]) / (2 * $units)}]
            set y [expr {double([lindex $point 1] + [lindex $origin 1]) / (2 * $units)}]
            lappend loads [list "[$inst getName]/$port" $x $y]
        }
        if {[llength $loads] < 2 || [llength $loads] > 8} {error "clock branch must have 2 to 8 loads"}
        lappend plans [list $name $net_name $loads]
    }
    set count 0
    set f [open $journal w]
    puts $f "driver,load,buffer,x_um,y_um"
    foreach plan $plans {
        lassign $plan driver net_name loads
        foreach load $loads {
            lassign $load pin x y
            set name ${prefix}_${count}
            insert_buffer -buffer_cell sg13g2_buf_4 -load_pins [list $pin] \
                -location [list $x $y] -buffer_name $name -net_name ${name}_net
            set added [get_name [get_nets -of_objects [get_pins $pin]]]
            if {$added == $net_name} {error "clock load was not rewired"}
            [$block findNet $added] setSigType CLOCK
            puts $f "$driver,$pin,$name,$x,$y"
            incr count
        }
    }
    close $f
    return $count
}
