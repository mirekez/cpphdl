# Optimization margin only. OpenRCX extraction and final STA use the unmodified
# technology model. Recompute from LEF on each call so margins never compound.
proc apply_routing_capacitance_margin {factor technology_lef} {
    if {![string is double -strict $factor] || !($factor >= 1 && $factor <= 5)} {
        error "routing capacitance margin must be finite and between 1 and 5"
    }
    # set_layer_rc changes OpenDB's layer capacitance as well as STA's view.
    # Read the original PDK file, including when a resumed database carries
    # an earlier optimization margin. Never modify that source technology.
    set f [open $technology_lef r]
    set original [read $f]
    close $f
    foreach name {Metal1 Metal2 Metal3 Metal4 Metal5 TopMetal1 TopMetal2} {
        set pattern [format {(?s)\nLAYER %s\s*\n(.*?)\nEND %s\s} $name $name]
        if {![regexp $pattern $original -> body] ||
            ![regexp {\n\s*WIDTH\s+(\S+)\s*;} $body -> width] ||
            ![regexp {RESISTANCE\s+RPERSQ\s+(\S+)\s*;} $body -> sheet_resistance] ||
            ![regexp {CAPACITANCE\s+CPERSQDIST\s+(\S+)\s*;} $body -> area] ||
            ![regexp {EDGECAPACITANCE\s+(\S+)\s*;} $body -> edge]} {
            error "missing IHP routing layer RC: $name"
        }
        set capacitance [expr {$width * $area + 2 * $edge}]
        if {$width <= 0 || $capacitance <= 0} {error "invalid IHP layer RC: $name"}
        # An omitted resistance is reset to zero by set_layer_rc. LEF uses
        # ohms and pF; convert to the loaded Liberty UI units explicitly.
        set resistance [sta::resistance_sta_ui [expr {$sheet_resistance / $width}]]
        set capacitance [sta::capacitance_sta_ui [expr {$factor * $capacitance * 1e-12}]]
        set_layer_rc -layer $name -resistance $resistance -capacitance $capacitance
    }
    set_wire_rc -signal -layer Metal3
    set_wire_rc -clock -layer Metal5
    puts "Routing optimization capacitance margin: $factor; extraction model unchanged"
}
