# Factor the admission AND so the raw-clock qualifier crosses two inverting
# stages. Reference readiness and enable can settle through their own AND.
# This preserves the truth table and asynchronous withdrawal of admission.
proc prepare_processing_clock_output {} {
    set block [ord::get_db_block]
    if {[$block getName] != "ProcessingClockControl"} {error "wrong admission-output top"}
    set port [$block findBTerm ready_pcs_out]
    if {$port == "NULL"} {error "missing admission output"}
    set output [$port getNet]
    set drivers {}
    foreach pin [$output getITerms] {
        if {[$pin getIoType] == "OUTPUT"} {lappend drivers $pin}
    }
    if {[llength $drivers] != 1} {error "ambiguous admission driver"}
    set old [[lindex $drivers 0] getInst]
    if {![regexp {^sg13g2_and3_[12]$} [[$old getMaster] getName]]} {
        error "unexpected admission function"
    }
    set inputs {}
    foreach name {A B C} {
        set net [[$old findITerm $name] getNet]
        dict set inputs [$net getName] $net
    }
    if {[lsort [dict keys $inputs]] != {enable_in qualified_ready ready}} {
        error "unexpected admission inputs"
    }
    foreach name {admission_allow admission_nand admission_driver} {
        if {[$block findInst $name] != "NULL"} {error "admission instance already exists"}
    }
    foreach name {admission_enabled admission_inverted} {
        if {[$block findNet $name] != "NULL"} {error "admission net already exists"}
    }
    foreach name {sg13g2_and2_1 sg13g2_nand2_2 sg13g2_inv_4} {
        if {[[ord::get_db] findMaster $name] == "NULL"} {error "missing admission cell"}
    }
    set allow [odb::dbInst_create $block [[ord::get_db] findMaster sg13g2_and2_1] admission_allow]
    set nand [odb::dbInst_create $block [[ord::get_db] findMaster sg13g2_nand2_2] admission_nand]
    set driver [odb::dbInst_create $block [[ord::get_db] findMaster sg13g2_inv_4] admission_driver]
    set enabled [odb::dbNet_create $block admission_enabled]
    set inverted [odb::dbNet_create $block admission_inverted]
    [$allow findITerm A] connect [dict get $inputs enable_in]
    [$allow findITerm B] connect [dict get $inputs ready]
    [$allow findITerm X] connect $enabled
    [$nand findITerm A] connect [dict get $inputs qualified_ready]
    [$nand findITerm B] connect $enabled
    [$nand findITerm Y] connect $inverted
    [$driver findITerm A] connect $inverted
    [$driver findITerm Y] connect $output
    odb::dbInst_destroy $old
}
