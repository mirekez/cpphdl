# Evaluate drive-strength changes using the routed receiver's extracted RC.
# This produces a candidate, not qualified timing: changed cells must subsequently
# be legalized, routed, re-extracted and independently checked.
set input $::env(RECEIVER_ROUTED_WORK)
set work $::env(REPEATER_WORK)
set libroot "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$libroot/lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$libroot/lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$input/core.odb"
if {[[ord::get_db_block] getName] != "PCSReceiver"} { error "receiver-only sizing diagnostic" }
read_sdc "$work/input.sdc"
set_propagated_clock [all_clocks]
# Restore the router's net map before its resize callbacks are installed.
# read_db restores guides but does not initialize that in-memory map.
write_guides "$work/restored.guide"
read_guides "$work/restored.guide"
read_spef -corner slow "$input/core.spef"
read_spef -corner fast "$input/core.spef"
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5

proc receiver_sizing_reports {prefix} {
    report_worst_slack -max -digits 9 > "${prefix}_setup.rpt"
    report_worst_slack -min -digits 9 > "${prefix}_hold.rpt"
    report_checks -path_delay max -group_path_count 20 -format full_clock_expanded \
        -fields {slew cap fanout input_pins} -digits 9 > "${prefix}_paths.rpt"
    report_check_types -max_slew -max_capacitance -max_fanout -violators > "${prefix}_electrical.rpt"
}

write_verilog "$work/before.v"
receiver_sizing_reports "$work/before"
repair_timing -setup -sequence sizeup -setup_margin 0.3 -max_iterations 100 -max_passes 30 \
    -skip_last_gasp -skip_vt_swap -skip_crit_vt_swap -verbose
# The optimizer can update its own estimates on changed nets. Report the
# candidate against the original extracted wires and the changed pin loads.
read_spef -corner slow "$input/core.spef"
read_spef -corner fast "$input/core.spef"
receiver_sizing_reports "$work/sized"
write_verilog "$work/after.v"
write_db "$work/sized.odb"
write_sdc "$work/sized.sdc"
puts "CPPHDL: extracted-RC sizing candidate requires fresh physical qualification"
exit
