# Shape this small controller's clock tree for its 800 MHz acquisition budget.
# The readiness flop has a short input qualifier and a tight output deadline;
# give it an early branch, then recheck every setup/hold and electrical path.
proc repair_processing_clock_tree {} {
    set block [ord::get_db_block]
    if {[$block getName] != "ProcessingClockControl"} {error "wrong top for controller clock repair"}
    foreach clock {pcs_clk ref_clk} {
        if {[llength [get_cells -quiet clkbuf_0_$clock]] != 1} {error "missing controller clock root"}
    }
    set ready_drivers [get_pins -of_objects [get_nets qualified_ready] -filter {direction == output}]
    if {[llength $ready_drivers] != 1} {error "ambiguous qualified-ready driver"}
    set ready_cell [lindex [get_cells -of_objects [lindex $ready_drivers 0]] 0]
    if {![regexp {^sg13g2_dfrbpq_[12]$} [get_property $ready_cell ref_name]]} {
        error "unexpected readiness flop"
    }
    if {[$block findInst clkbuf_readiness] != "NULL" ||
        [$block findNet readiness_clock] != "NULL"} {
        error "readiness clock branch already exists"
    }
    set remove {}
    foreach instance [get_cells -quiet clkbuf_*] {
        set name [get_name $instance]
        if {[regexp {^clkbuf_([0-9]+)_.*(pcs_clk|ref_clk)$} $name -> level clock]} {
            if {$level == 1 || $level == 3} {
                if {![regexp {^sg13g2_buf_[0-9]+$} [get_property $instance ref_name]]} {
                    error "non-buffer controller clock instance"
                }
                lappend remove $name
            }
        }
    }
    # remove_buffers expects expanded instance names, not a nested collection.
    # An unmatched/empty selection can mean all buffers in this OpenROAD build.
    if {[llength $remove] == 0} {error "empty controller clock compaction set"}
    set before [llength [get_cells *]]
    remove_buffers {*}$remove
    if {[llength [get_cells *]] != $before - [llength $remove]} {
        error "unexpected controller buffer removal count"
    }
    foreach clock {pcs_clk ref_clk} {
        if {[llength [get_cells -quiet clkbuf_0_$clock]] != 1} {error "controller clock root was removed"}
    }
    foreach instance [get_cells -quiet clkbuf_*] {
        set name [get_name $instance]
        if {[regexp {^clkbuf_([0-9]+)_.*(pcs_clk|ref_clk)$} $name -> level clock]} {
            if {$level == 0} {replace_cell $name sg13g2_buf_16}
            if {$level == 2} {replace_cell $name sg13g2_buf_8}
            if {$level == 4 || $level == 5} {replace_cell $name sg13g2_buf_4}
        }
    }
    set ready_instance [$block findInst [get_name $ready_cell]]
    set ready_clock [$ready_instance findITerm CLK]
    # A local branch drives only the readiness flop. The shared root's larger
    # load spent the output setup margin; direct input clocking spent the input
    # setup margin. Check both paths after routing this real intermediate buffer.
    set branch [odb::dbInst_create $block [[ord::get_db] findMaster sg13g2_buf_8] clkbuf_readiness]
    set root_net [odb::dbNet_create $block readiness_clock]
    [$branch findITerm A] connect [[$block findBTerm pcs_clk] getNet]
    [$branch findITerm X] connect $root_net
    set location [$ready_instance getLocation]
    $branch setLocation {*}$location
    $branch setPlacementStatus PLACED
    $ready_clock disconnect
    $ready_clock connect $root_net
    puts "Compacted [llength $remove] controller clock buffers; added local branch for [get_name $ready_cell]"
}
