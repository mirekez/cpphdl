# The pinned May 2026 router can restart fifty congestion iterations for each
# optional CTS NDR it drops. Recovery uses ordinary PDK spacing and reruns all
# extracted timing, replay and geometry checks. Explicit custom rules survive.
proc remove_receiver_clock_ndr {clock_nets report_path} {
    set changes {}
    foreach net $clock_nets {
        set rule [$net getNonDefaultRule]
        if {$rule == "NULL"} { continue }
        set name [$rule getName]
        if {![regexp {^CTS_NDR_[0-9]+$} $name] || [$rule getHardSpacing]} {
            error "refusing to remove custom/hard clock rule $name on [$net getName]"
        }
        lappend changes [list $net $name]
    }
    # Validate every candidate before changing the database.
    set output [open $report_path w]
    puts $output "net,previous_rule,new_rule"
    foreach change $changes {
        lassign $change net previous
        $net setNonDefaultRule NULL
        if {[$net getNonDefaultRule] != "NULL"} { error "clock NDR removal failed" }
        puts $output "[$net getName],$previous,PDK_default"
    }
    close $output
    puts "CPPHDL: removed [llength $changes] optional CTS clock spacing rules; final DRC/timing unchanged"
    return [llength $changes]
}
