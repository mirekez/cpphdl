# Emit the full combinational-loop inventory from a synthesized Tribe checkpoint.

proc env_or_default {name fallback} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return $::env($name)
    }
    return $fallback
}

set script_dir [file dirname [file normalize [info script]]]
set synth_dcp [file normalize [env_or_default TRIBE_SYNTH_DCP \
    [file join $script_dir vivado_tribe_timing checkpoints post_synth.dcp]]]
set report_file [file normalize [env_or_default TRIBE_LOOP_REPORT \
    [file join $script_dir tribe_check_timing_verbose.rpt]]]

open_checkpoint $synth_dcp
check_timing -verbose -loop_limit 1000 -file $report_file
report_drc -checks LUTLP-1 -file "${report_file}.drc"
