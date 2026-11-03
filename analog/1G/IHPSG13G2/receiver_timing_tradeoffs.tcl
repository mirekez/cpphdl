# Trial same-function drive changes with unchanged interconnect topology.
# The caller must prove functional equivalence and physically qualify the result.
proc receiver_timing_tradeoffs {trials journal hold_margin_ns {rc_model extracted}} {
    if {$rc_model ni {extracted global_routing}} { error "unsupported trial RC model: $rc_model" }
    set block [ord::get_db_block]
    if {[$block getName] != "PCSReceiver"} { error "receiver-only timing trials" }
    if {$rc_model == "global_routing"} { estimate_parasitics -global_routing }
    set log [open $journal w]
    puts $log "instance,before,after,setup_ns,hold_ns,tns_ns,accepted"
    flush $log
    set best_setup [sta::worst_slack_cmd max]
    set best_tns [sta::total_negative_slack_cmd max]
    foreach trial $trials {
        lassign $trial name target
        set instance [$block findInst $name]
        if {$instance == "NULL" || [$instance isFixed] || [$instance isDoNotTouch]} {
            error "missing or protected timing trial instance: $name"
        }
        set old [[$instance getMaster] getName]
        if {$old == $target} { continue }
        # replace_cell updates the timing graph and the OpenDB instance together.
        replace_cell $name $target
        # Global-route estimates retain reduced pin loads after replace_cell.
        # Rebuild RC before deciding: invalidating timing alone is insufficient.
        # Imported extracted networks use STA's pin-load updates and require an
        # independent final process reload; do not substitute estimated wires.
        if {$rc_model == "global_routing"} { estimate_parasitics -global_routing }
        set setup [sta::worst_slack_cmd max]
        set hold [sta::worst_slack_cmd min]
        set tns [sta::total_negative_slack_cmd max]
        set accept [expr {$hold >= $hold_margin_ns * 1e-9 &&
            $setup >= $best_setup - 1e-12 && $tns >= $best_tns - 1e-12 &&
            ($setup > $best_setup + 1e-12 || $tns > $best_tns + 1e-12)}]
        puts $log "$name,$old,$target,[expr {$setup * 1e9}],[expr {$hold * 1e9}],[expr {$tns * 1e9}],$accept"
        flush $log
        if {$accept} {
            set best_setup $setup
            set best_tns $tns
        } else {
            replace_cell $name $old
            # Restore both the cell and its electrical model, including when
            # this is the last trial. The caller must see the retained design.
            if {$rc_model == "global_routing"} { estimate_parasitics -global_routing }
        }
    }
    close $log
}
