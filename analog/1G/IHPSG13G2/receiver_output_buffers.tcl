# Isolate explicitly selected slow, single-strength data gates. This is an
# optimization candidate, never a timing waiver. Rebuild routes afterward.
proc receiver_output_buffers {pins journal {prefix receiver_output}} {
    set block [ord::get_db_block]
    if {[$block getName] != "PCSReceiver"} { error "receiver-only output buffering" }
    if {[llength $pins] > 128 || [llength [lsort -unique $pins]] != [llength $pins]} {
        error "expected at most 128 unique output pins"
    }
    if {![regexp {^receiver_output(_[0-9]+)?$} $prefix]} { error "invalid output-buffer prefix" }
    # OpenROAD may append a numeric suffix to the requested buffer name.
    # Reject any existing instance in this pass's namespace, not just exact names.
    foreach instance [$block getInsts] {
        if {[string match "${prefix}_*" [$instance getName]]} {
            error "output-buffer pass already exists"
        }
    }
    set clocks [dict create]
    foreach net [sta::find_all_clk_nets] { dict set clocks [$net getName] 1 }
    set output [open $journal w]
    puts $output "pin,buffer,master"
    set index 0
    foreach name $pins {
        set pin [get_pins -quiet $name]
        if {[llength $pin] != 1 || [get_property $pin direction] != "output"} {
            error "expected one data output: $name"
        }
        set cell [lindex [get_cells -of_objects $pin] 0]
        if {![regexp {^sg13g2_(xn?or2|o21ai|a221oi)_1$} [get_property $cell ref_name]]} {
            error "expected a supported single-strength gate: $name"
        }
        set instance [$block findInst [get_name $cell]]
        set net_name [get_name [get_nets -of_objects $pin]]
        set net [$block findNet $net_name]
        if {[$instance isFixed] || [$instance isDoNotTouch] || [$net isDoNotTouch] ||
            [$net isSpecial] || [$net getSigType] != "SIGNAL" || [dict exists $clocks $net_name]} {
            error "protected output cannot be buffered: $name"
        }
        # Two-strength buffer has a modest input load and takes the wire and
        # receiving-pin load away from the weak compound-gate output.
        set buffer ${prefix}_$index
        set signal ${prefix}_net_$index
        if {[$block findInst $buffer] != "NULL" || [$block findNet $signal] != "NULL"} {
            error "output buffer identifier already exists"
        }
        lassign [$instance getLocation] x y
        set units [$block getDbUnitsPerMicron]
        insert_buffer -buffer_cell sg13g2_buf_2 -net [get_nets -of_objects $pin] \
            -location [list [expr {double($x) / $units}] [expr {double($y) / $units}]] \
            -buffer_name $buffer -net_name $signal
        puts $output "$name,$buffer,sg13g2_buf_2"
        incr index
    }
    close $output
    return $index
}
