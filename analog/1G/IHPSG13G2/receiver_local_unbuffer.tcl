# Generate a small receiver ECO from explicit critical-path buffer names.
# Full endpoint equivalence, fresh routing and hold/setup checks follow.
proc receiver_local_buffer_candidates {names maximum_span_um {span_limit_um 60} {maximum_sinks 2}} {
    set block [ord::get_db_block]
    if {[$block getName] != "PCSReceiver"} { error "receiver-only local unbuffering" }
    if {![string is double -strict $span_limit_um] || !($span_limit_um > 0 && $span_limit_um <= 200) ||
        ![string is double -strict $maximum_span_um] || !($maximum_span_um > 0 && $maximum_span_um <= $span_limit_um)} {
        error "local buffer span must fit its explicit limit (default 60, maximum 200 um)"
    }
    if {![string is integer -strict $maximum_sinks] || $maximum_sinks < 1 || $maximum_sinks > 4} {
        error "local buffer sink bound must be an integer from 1 to 4"
    }
    if {[llength $names] > 200 || [llength [lsort -unique $names]] != [llength $names]} {
        error "expected at most 200 unique explicit buffer names"
    }
    set clocks [dict create]
    foreach net [sta::find_all_clk_nets] { dict set clocks [$net getName] 1 }
    set protected [dict create]
    set result {}
    set units [$block getDbUnitsPerMicron]
    if {$units <= 0} { error "invalid receiver distance units" }
    foreach name $names {
        if {![regexp {^(place|wire)[0-9]+$} $name]} { error "expected a data repair buffer" }
        set inst [$block findInst $name]
        if {$inst == "NULL"} { error "missing explicitly selected buffer: $name" }
        if {[dict exists $protected $name] || [$inst isDoNotTouch] || [$inst getPlacementStatus] != "PLACED"} { continue }
        if {![regexp {^sg13g2_buf_(1|2|4|8|16)$} [[$inst getMaster] getName]]} { continue }
        set a [$inst findITerm A]
        set x [$inst findITerm X]
        if {$a == "NULL" || $x == "NULL"} { error "unexpected buffer interface" }
        set input [$a getNet]
        set output [$x getNet]
        if {$input == "NULL" || $output == "NULL" || $input == $output} { continue }
        set valid 1
        foreach net [list $input $output] {
            if {[$net isSpecial] || [$net isDoNotTouch] || [$net getSigType] != "SIGNAL" ||
                [dict exists $clocks [$net getName]] || [llength [$net getBTerms]] != 0} { set valid 0 }
        }
        if {!$valid || [llength [$input getITerms]] != 2} { continue }
        set source "NULL"
        foreach pin [$input getITerms] {
            if {$pin != $a && [[$pin getMTerm] getIoType] == "OUTPUT"} { set source [$pin getInst] }
        }
        if {$source == "NULL" || $source == $inst} { continue }
        # Keep register loading unchanged in this limited ECO.
        if {[regexp {^sg13g2_(sd|dfr)} [[$source getMaster] getName]]} { continue }
        set neighbors [list $source]
        set sinks {}
        foreach pin [$output getITerms] {
            if {$pin == $x} { continue }
            if {[[$pin getMTerm] getIoType] != "INPUT"} { set valid 0 }
            set sink [$pin getInst]
            if {$sink == $source || $sink == $inst} { set valid 0 }
            lappend sinks $sink
            lappend neighbors $sink
        }
        if {!$valid || [llength $sinks] < 1 || [llength $sinks] > $maximum_sinks} { continue }
        set box [$source getBBox]
        set cx [expr {([$box xMin] + [$box xMax]) / 2.0}]
        set cy [expr {([$box yMin] + [$box yMax]) / 2.0}]
        set span 0
        foreach neighbor $neighbors {
            if {[$neighbor isDoNotTouch] || [$neighbor getPlacementStatus] != "PLACED" ||
                [dict exists $protected [$neighbor getName]]} { set valid 0 }
            set box [$neighbor getBBox]
            set distance [expr {(abs(([$box xMin] + [$box xMax]) / 2.0 - $cx) +
                                abs(([$box yMin] + [$box yMax]) / 2.0 - $cy)) / $units}]
            set span [expr {max($span, $distance)}]
        }
        if {!$valid || $span > $maximum_span_um} { continue }
        lappend result [list $name [[$inst getMaster] getName] $span [$source getName] [llength $sinks]]
        foreach neighbor [concat [list $inst] $neighbors] { dict set protected [$neighbor getName] 1 }
    }
    return $result
}

proc remove_receiver_local_buffers {names maximum_span_um report_path {span_limit_um 60} {maximum_sinks 2}} {
    set rows [receiver_local_buffer_candidates $names $maximum_span_um $span_limit_um $maximum_sinks]
    if {[llength $rows] == 0} { error "no eligible short receiver data buffers" }
    set selected {}
    foreach row $rows { lappend selected [lindex $row 0] }
    remove_buffers {*}$selected
    set report [open $report_path w]
    puts $report "instance,master,span_um,source,sinks"
    foreach row $rows {
        if {[[ord::get_db_block] findInst [lindex $row 0]] != "NULL"} { error "buffer survived removal" }
        puts $report [join $row ,]
    }
    close $report
    return [llength $rows]
}
