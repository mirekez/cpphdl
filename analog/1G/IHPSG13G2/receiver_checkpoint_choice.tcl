# Compare fresh route estimates under identical nominal and enlarged-C views.
# A retained diagnostic may still have negative enlarged-C setup. It is never
# promoted to physical qualification by this selection predicate.
proc receiver_checkpoint_improves {before after} {
    foreach values [list $before $after] {
        foreach key {nominal_setup nominal_hold margin_setup margin_hold margin_tns} {
            if {![dict exists $values $key] ||
                ![string is double -strict [dict get $values $key]] ||
                !([dict get $values $key] > -1e6 && [dict get $values $key] < 1e6)} {
                error "missing finite checkpoint metric: $key"
            }
        }
        foreach key {nominal_electrical margin_electrical} {
            if {![dict exists $values $key] || [dict get $values $key] ni {0 1}} {
                error "missing checkpoint electrical result: $key"
            }
        }
    }
    return [expr {
        [dict get $after nominal_electrical] && [dict get $after margin_electrical] &&
        [dict get $after nominal_setup] >= 0.1 &&
        [dict get $after nominal_hold] >= 0.1 && [dict get $after margin_hold] >= 0.1 &&
        [dict get $after nominal_setup] >= [dict get $before nominal_setup] - 0.001 &&
        [dict get $after margin_setup] >= [dict get $before margin_setup] - 0.001 &&
        [dict get $after margin_tns] >= [dict get $before margin_tns] - 0.001 &&
        ([dict get $after margin_setup] > [dict get $before margin_setup] + 0.001 ||
         [dict get $after margin_tns] > [dict get $before margin_tns] + 0.001)
    }]
}
