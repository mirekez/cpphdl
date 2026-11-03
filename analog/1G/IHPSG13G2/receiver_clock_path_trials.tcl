# Select the two clock-buffer levels nearest reported critical registers.
# Only same-function drive trials follow; setup/TNS and hold guard acceptance.
proc receiver_clock_path_trials {path_report} {
    set block [ord::get_db_block]
    if {[$block getName] != "PCSReceiver"} { error "receiver-only clock trials" }
    set f [open $path_report r]
    set text [read $f]
    close $f
    set endpoints [regexp -all -inline -line {^(Startpoint|Endpoint): ([A-Za-z0-9_]+) } $text]
    if {[llength $endpoints] == 0} { error "no reported critical registers" }
    set visited [dict create]
    set trials {}
    foreach {whole role name} $endpoints {
        set reg [$block findInst $name]
        if {$reg == "NULL"} { error "missing reported critical register: $name" }
        set pin [$reg findITerm CLK]
        if {$pin == "NULL"} { error "critical register lacks CLK: $name" }
        set net [$pin getNet]
        for {set depth 0} {$depth < 2} {incr depth} {
            if {$net == "NULL" || [$net isDoNotTouch] || [$net isSpecial]} { break }
            set drivers {}
            foreach term [$net getITerms] {
                if {[[$term getMTerm] getIoType] == "OUTPUT"} { lappend drivers [$term getInst] }
            }
            if {[llength $drivers] != 1} { break }
            set inst [lindex $drivers 0]
            set master [[$inst getMaster] getName]
            if {[$inst isFixed] || [$inst isDoNotTouch] ||
                ![regexp {^sg13g2_buf_(1|2|4|8|16)$} $master]} { break }
            set cell [$inst getName]
            if {![dict exists $visited $cell]} {
                dict set visited $cell 1
                foreach strength {1 2 4 8 16} {
                    set target sg13g2_buf_$strength
                    if {$target != $master && [llength $trials] < 200} {
                        lappend trials [list $cell $target]
                    }
                }
            }
            set input [$inst findITerm A]
            if {$input == "NULL"} { error "clock buffer lacks A" }
            set net [$input getNet]
        }
        if {[llength $trials] >= 200} { break }
    }
    return $trials
}
