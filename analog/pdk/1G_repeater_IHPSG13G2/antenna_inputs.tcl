# Global-route estimates missed a long, detailed-route input to a small hold
# gate. Protect each such primary-input receiver locally, using real PDK
# diffusion. The final KLayout antenna check remains the acceptance criterion.
proc protect_input_delay_gates {patterns audit} {
    set block [ord::get_db_block]
    set dbu [$block getDbUnitsPerMicron]
    set master [[ord::get_db] findMaster sg13g2_antennanp]
    if {$master == "NULL"} { error "IHP antenna protection master is missing" }
    set targets {}
    set original {}
    foreach instance [$block getInsts] {
        if {[$instance getPlacementStatus] == "PLACED"} { lappend original $instance }
    }
    set added 0
    set serial 0
    foreach net [$block getNets] {
        set selected 0
        foreach pattern $patterns {
            if {[string match $pattern [$net getName]]} { set selected 1; break }
        }
        if {!$selected} { continue }
        set input 0
        foreach port [$net getBTerms] {
            if {[$port getIoType] == "INPUT"} { set input 1 }
        }
        if {!$input} { continue }
        foreach term [$net getITerms] {
            set victim [$term getInst]
            if {[[$victim getMaster] getName] != "sg13g2_dlygate4sd3_1" ||
                [[$term getMTerm] getIoType] != "INPUT"} { continue }
            lassign [$victim getLocation] vx vy
            set diode "NULL"
            foreach other [$net getITerms] {
                set instance [$other getInst]
                if {[[$instance getMaster] getName] != "sg13g2_antennanp" ||
                    [[$other getMTerm] getName] != "A"} { continue }
                lassign [$instance getLocation] x y
                if {abs($vx - $x) + abs($vy - $y) <= 25 * $dbu} { set diode $instance; break }
            }
            if {$diode == "NULL"} {
                while {[$block findInst INPUT_ANTENNA_$serial] != "NULL"} { incr serial }
                set diode [odb::dbInst_create $block $master INPUT_ANTENNA_$serial]
                incr serial
                [$diode findITerm A] connect $net
                $diode setOrient [$victim getOrient]
                $diode setLocation [expr {$vx - [$master getWidth] - round(.96 * $dbu)}] $vy
                $diode setPlacementStatus PLACED
                incr added
            }
            lappend targets [list [$term getName] [$net getName] $victim $diode]
        }
    }
    if {$added} {
        # Preserve the already timed placement. Only the new protection cells
        # need whitespace; moving all old cells can cascade through dense rows.
        foreach instance $original { $instance setPlacementStatus FIRM }
        try {
            detailed_placement -max_displacement {10 10}
        } finally {
            foreach instance $original { $instance setPlacementStatus PLACED }
        }
    }
    set output [open $audit w]
    puts $output "pin,net,diode,distance_um"
    foreach target $targets {
        lassign $target pin net victim diode
        lassign [$victim getLocation] vx vy
        lassign [$diode getLocation] x y
        set distance [expr {double(abs($vx - $x) + abs($vy - $y)) / $dbu}]
        puts $output "$pin,$net,[$diode getName],$distance"
        # The legalizer permits 10 um in each axis from the nearby target.
        if {$distance > 25} { close $output; error "protection diode remains remote from $pin" }
    }
    close $output
    puts "Input antenna protection: [llength $targets] receiver gates, $added added PDK diodes"
    return [llength $targets]
}
