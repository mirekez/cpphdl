# Reduce excess data-path hold delay without changing Boolean connectivity.
# Every candidate uses refreshed RC; final acceptance still needs extraction.
proc receiver_hold_trim {setup_target_ns hold_margin_ns rounds journal {max_trials 18} {max_seconds 600}} {
    if {![string is double -strict $setup_target_ns] || !($setup_target_ns > 0 && $setup_target_ns <= 2) ||
        ![string is double -strict $hold_margin_ns] || !($hold_margin_ns >= 0.1 && $hold_margin_ns <= 10) ||
        ![string is integer -strict $rounds] || $rounds < 1 || $rounds > 3 ||
        ![string is integer -strict $max_trials] || $max_trials < 1 || $max_trials > 200 ||
        ![string is integer -strict $max_seconds] || $max_seconds < 1 || $max_seconds > 900} {
        error "invalid receiver hold-trim controls"
    }
    set block [ord::get_db_block]
    if {[$block getName] != "PCSReceiver"} {error "receiver-only hold trimming"}
    set clocks [dict create]
    foreach net [sta::find_all_clk_nets] {dict set clocks [$net getName] 1}
    set log [open $journal w]
    puts $log "round,instance,before,after,setup_ns,hold_ns,accepted"
    set total 0
    set trials 0
    set deadline [expr {[clock milliseconds] + 1000 * $max_seconds}]
    # Divide the trial allowance across passes so critical paths are selected
    # again after delay reductions, instead of spending it on stale paths.
    set per_pass [expr {($max_trials + $rounds - 1) / $rounds}]
    estimate_parasitics -global_routing
    for {set pass 1} {$pass <= $rounds} {incr pass} {
        set candidates [dict create]
        foreach path [find_timing_paths -path_delay max -slack_max $setup_target_ns -group_path_count 200] {
            foreach point [get_property $path points] {
                foreach cell [get_cells -quiet -of_objects [get_property $point pin]] {
                    if {[dict size $candidates] < 200 &&
                        [regexp {^sg13g2_dlygate4sd([123])_1$} [get_property $cell ref_name] -> strength]} {
                        dict set candidates [get_name $cell] $strength
                    }
                }
            }
        }
        set accepted 0
        set pass_trials 0
        dict for {name strength} $candidates {
            if {$trials >= $max_trials || $pass_trials >= $per_pass ||
                [clock milliseconds] >= $deadline} {break}
            set inst [$block findInst $name]
            if {[$inst isFixed] || [$inst isDoNotTouch]} {continue}
            set protected 0
            foreach pin [$inst getITerms] {
                # Every powered production cell touches special supply nets.
                # They remain connected through the same-function replacement;
                # only signal pins determine whether this data edit is protected.
                if {[[$pin getMTerm] getSigType] in {POWER GROUND}} {continue}
                set net [$pin getNet]
                if {$net != "NULL" && ([$net isDoNotTouch] || [$net isSpecial] ||
                    [dict exists $clocks [$net getName]])} {set protected 1}
            }
            if {$protected} {continue}
            incr trials
            incr pass_trials
            # RC is fresh from initialization, the preceding trial or rollback.
            set before [sta::worst_slack_cmd max]
            set original sg13g2_dlygate4sd${strength}_1
            set reduced [expr {$strength - 1}]
            set target sg13g2_buf_1
            if {$reduced > 0} {set target sg13g2_dlygate4sd${reduced}_1}
            replace_cell $name $target
            estimate_parasitics -global_routing
            set setup [sta::worst_slack_cmd max]
            set hold [sta::worst_slack_cmd min]
            set keep [expr {$setup >= $before - 1e-12 && $hold >= $hold_margin_ns * 1e-9}]
            puts $log "$pass,$name,$original,$target,[expr {$setup * 1e9}],[expr {$hold * 1e9}],$keep"
            flush $log
            if {$keep} {
                incr accepted
                incr total
            } else {
                replace_cell $name $original
                estimate_parasitics -global_routing
            }
        }
        if {$accepted == 0} {break}
        detailed_placement
        check_placement -verbose
        global_route
        estimate_parasitics -placement
        estimate_parasitics -global_routing
        # Legalization can alter RC. Stop on a lost hold reserve; the caller
        # retains the diagnostic and must reject it before detailed routing.
        if {[sta::worst_slack_cmd min] < $hold_margin_ns * 1e-9 ||
            $trials >= $max_trials || [clock milliseconds] >= $deadline} {break}
    }
    close $log
    puts "CPPHDL hold trim: $trials trials, $total accepted; bounded diagnostic only"
    return $total
}
