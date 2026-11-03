# Collapse short data-buffer branches only when the existing source is a strong
# buffer and the merged load has at most eight input pins. Clock/protected nets
# are excluded. The caller must legalize, rebuild RC and prove exact endpoints.
proc receiver_branch_buffers {names journal {span_um 120}} {
    if {![string is double -strict $span_um] || !($span_um > 0 && $span_um <= 200)} {
        error "branch neighbor span must be in (0, 200] um"
    }
    set block [ord::get_db_block]
    if {[$block getName] != "PCSReceiver"} { error "receiver-only branch repair" }
    set clocks [dict create]
    foreach net [sta::find_all_clk_nets] { dict set clocks [$net getName] 1 }
    set protected [dict create]
    set selected {}
    set rows {}
    set units [$block getDbUnitsPerMicron]
    foreach name [lsort -unique $names] {
        if {[llength $selected] >= 32} { break }
        if {![regexp {^(place|wire)[0-9]+$} $name]} { continue }
        set inst [$block findInst $name]
        if {$inst == "NULL" || [dict exists $protected $name] || [$inst isFixed] ||
            [$inst isDoNotTouch] || [$inst getPlacementStatus] != "PLACED" ||
            ![regexp {^sg13g2_buf_(1|2|4|8|16)$} [[$inst getMaster] getName]]} { continue }
        set a [$inst findITerm A]
        set x [$inst findITerm X]
        set input [$a getNet]
        set output [$x getNet]
        if {$input == "NULL" || $output == "NULL" || $input == $output} { continue }
        set valid 1
        foreach net [list $input $output] {
            if {[$net isSpecial] || [$net isDoNotTouch] || [$net getSigType] != "SIGNAL" ||
                [llength [$net getBTerms]] || [dict exists $clocks [$net getName]]} { set valid 0 }
        }
        if {!$valid} { continue }
        set drivers {}
        set input_loads 0
        set neighbors {}
        foreach term [$input getITerms] {
            set kind [[$term getMTerm] getIoType]
            if {$kind == "OUTPUT"} {
                lappend drivers [$term getInst]
            } elseif {$kind == "INPUT"} {
                incr input_loads
                lappend neighbors [$term getInst]
            } else { set valid 0 }
        }
        if {!$valid || [llength $drivers] != 1} { continue }
        set source [lindex $drivers 0]
        if {$source == $inst || ![regexp {^sg13g2_buf_(8|16)$} [[$source getMaster] getName]]} { continue }
        set output_loads 0
        foreach term [$output getITerms] {
            if {$term == $x} { continue }
            if {[[$term getMTerm] getIoType] != "INPUT" || [$term getInst] == $source} { set valid 0 }
            incr output_loads
            lappend neighbors [$term getInst]
        }
        set merged [expr {$input_loads - 1 + $output_loads}]
        if {!$valid || $input_loads < 2 || $output_loads < 1 || $merged > 8} { continue }
        set neighbors [lsort -unique [concat $neighbors [list $source $inst]]]
        set box [$source getBBox]
        set cx [expr {([$box xMin] + [$box xMax]) / 2.0}]
        set cy [expr {([$box yMin] + [$box yMax]) / 2.0}]
        set span 0
        foreach neighbor $neighbors {
            if {[$neighbor isFixed] || [$neighbor isDoNotTouch] ||
                [$neighbor getPlacementStatus] != "PLACED" ||
                [dict exists $protected [$neighbor getName]]} { set valid 0 }
            set box [$neighbor getBBox]
            set distance [expr {(abs(([$box xMin] + [$box xMax]) / 2.0 - $cx) +
                                abs(([$box yMin] + [$box yMax]) / 2.0 - $cy)) / $units}]
            set span [expr {max($span, $distance)}]
        }
        if {!$valid || $span > $span_um} { continue }
        lappend selected $name
        lappend rows [list $name [$source getName] $input_loads $output_loads $merged $span]
        foreach neighbor $neighbors { dict set protected [$neighbor getName] 1 }
    }
    set f [open $journal w]
    puts $f "instance,source,input_loads,output_loads,merged_loads,span_um"
    foreach row $rows { puts $f [join $row ,] }
    close $f
    if {[llength $selected]} { remove_buffers {*}$selected }
    foreach name $selected {
        if {[$block findInst $name] != "NULL"} { error "branch buffer survived removal" }
    }
    return [llength $selected]
}
