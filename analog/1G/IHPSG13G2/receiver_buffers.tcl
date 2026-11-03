# Remove short, unbranched chains created by wire-delay repair. This is an ECO
# candidate generator; complete connectivity, hold/setup and routing checks follow.
proc receiver_buffer_strength {inst} {
    if {$inst == "NULL"} { return 0 }
    if {![regexp {^sg13g2_buf_(1|2|4|8|16)$} [[$inst getMaster] getName] unused strength]} {
        return 0
    }
    return $strength
}

proc receiver_buffer_neighbor {net pin role clocks} {
    if {$net == "NULL" || [$net isSpecial] || [$net isDoNotTouch]} { return "NULL" }
    if {[dict exists $clocks [$net getName]] || [llength [$net getBTerms]] != 0} { return "NULL" }
    set terminals [$net getITerms]
    if {[llength $terminals] != 2} { return "NULL" }
    foreach terminal $terminals {
        if {$terminal != $pin && [[$terminal getMTerm] getIoType] == $role} {
            return [$terminal getInst]
        }
    }
    return "NULL"
}

proc receiver_series_buffer_candidates {block clock_nets maximum_span_um} {
    if {![string is double -strict $maximum_span_um] || !($maximum_span_um > 0 && $maximum_span_um <= 20)} {
        error "receiver short-chain span must be in (0, 20] um"
    }
    set clocks [dict create]
    foreach net $clock_nets { dict set clocks [$net getName] 1 }
    set protected [dict create]
    set result {}
    set units [$block getDbUnitsPerMicron]
    if {$units <= 0} { error "receiver database must define positive distance units" }
    foreach inst [$block getInsts] {
        # Only wire-repair buffers are eligible; clock nets are protected below.
        set name [$inst getName]
        if {![regexp {^wire[0-9]+$} $name] || [dict exists $protected $name]} { continue }
        set strength [receiver_buffer_strength $inst]
        if {$strength == 0 || [$inst isDoNotTouch] || [$inst getPlacementStatus] != "PLACED"} { continue }
        set a [$inst findITerm A]
        set x [$inst findITerm X]
        if {$a == "NULL" || $x == "NULL"} { error "IHP buffer has an unexpected interface" }
        set upstream [receiver_buffer_neighbor [$a getNet] $a OUTPUT $clocks]
        set downstream [receiver_buffer_neighbor [$x getNet] $x INPUT $clocks]
        if {[receiver_buffer_strength $upstream] < $strength || [receiver_buffer_strength $downstream] == 0} { continue }
        if {$upstream == $downstream || $upstream == $inst || $downstream == $inst} { continue }
        if {[$upstream isDoNotTouch] || [$downstream isDoNotTouch]} { continue }
        if {[$upstream getPlacementStatus] != "PLACED" || [$downstream getPlacementStatus] != "PLACED"} { continue }
        if {[dict exists $protected [$upstream getName]] || [dict exists $protected [$downstream getName]]} { continue }
        set before [$upstream getBBox]
        set after [$downstream getBBox]
        set span [expr {(abs(([$before xMin] + [$before xMax]) - ([$after xMin] + [$after xMax])) +
                        abs(([$before yMin] + [$before yMax]) - ([$after yMin] + [$after yMax]))) / (2.0 * $units)}]
        if {$span > $maximum_span_um} { continue }
        lappend result [list $name [[$inst getMaster] getName] $span [$upstream getName] [$downstream getName]]
        foreach cell [list $inst $upstream $downstream] { dict set protected [$cell getName] 1 }
    }
    return $result
}

proc remove_receiver_series_buffers {maximum_span_um passes report_path} {
    if {[[ord::get_db_block] getName] != "PCSReceiver"} { error "short-chain ECO is receiver-only" }
    if {![string is integer -strict $passes] || $passes < 1 || $passes > 8} { error "invalid ECO pass count" }
    set output [open $report_path w]
    puts $output "pass,instance,master,span_um,upstream,downstream"
    set total 0
    try {
        for {set pass 0} {$pass < $passes} {incr pass} {
            set rows [receiver_series_buffer_candidates [ord::get_db_block] [sta::find_all_clk_nets] $maximum_span_um]
            if {[llength $rows] == 0} { break }
            set names {}
            foreach row $rows { lappend names [lindex $row 0] }
            remove_buffers {*}$names
            foreach row $rows {
                if {[[ord::get_db_block] findInst [lindex $row 0]] != "NULL"} { error "selected buffer survived removal" }
                puts $output "$pass,[join $row ,]"
            }
            incr total [llength $rows]
            puts "CPPHDL: removed [llength $rows] short series buffers in pass $pass"
        }
    } finally { close $output }
    return $total
}
