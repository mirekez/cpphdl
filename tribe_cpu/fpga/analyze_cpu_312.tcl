# Standalone physical timing analysis for one generated Tribe core at 312 MHz.
#
# TRIBE_RUN_DIR may be used to keep measurements from successive RTL changes
# separate.  This OOC run is intentionally independent of L2 placement so a
# CPU pipeline change can be evaluated much faster than routing four cores and
# the shared cache.

proc env_or_default {name fallback} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return $::env($name)
    }
    return $fallback
}

set script_dir [file dirname [file normalize [info script]]]
set rtl_dir [file join $script_dir cpphdl_tribe256_multicore generated]
set run_dir [file normalize [env_or_default TRIBE_RUN_DIR \
    [file join $script_dir vivado_cpu_312]]]
set report_dir [file join $run_dir reports]
set checkpoint_dir [file join $run_dir checkpoints]
set part xc7k325tffg676-3
set cpu_period [expr {1000.0 / 312.0}]

file mkdir $report_dir
file mkdir $checkpoint_dir

set prerequisite_packages [list \
    Predef_pkg.sv \
    Axi4WriteAddress32_4_pkg.sv \
    Axi4WriteData256_pkg.sv \
    Axi4WriteResponseReady_pkg.sv \
    Axi4ReadAddress32_4_pkg.sv \
    Axi4ReadDataReady_pkg.sv \
    Axi4WriteAddressReady_pkg.sv \
    Axi4WriteDataReady_pkg.sv \
    Axi4WriteResponse4_pkg.sv \
    Axi4ReadAddressReady_pkg.sv \
    Axi4ReadData4_256_pkg.sv \
    CacheRequest_pkg.sv \
    L1CachePerf_pkg.sv]

set prereq_paths {}
foreach package $prerequisite_packages {
    lappend prereq_paths [file join $rtl_dir $package]
}
read_verilog -sv $prereq_paths

set deferred_packages {}
foreach source [lsort [glob -nocomplain [file join $rtl_dir *_pkg.sv]]] {
    if {[lsearch -exact $prereq_paths $source] < 0} {
        lappend deferred_packages $source
    }
}
read_verilog -sv $deferred_packages

set module_sources {}
foreach source [lsort [glob -nocomplain [file join $rtl_dir *.sv]]] {
    if {![string match "*_pkg.sv" $source]} {
        lappend module_sources $source
    }
}
read_verilog -sv $module_sources

synth_design -mode out_of_context -top Tribe -part $part \
    -flatten_hierarchy rebuilt
create_clock -name cpu_clk -period $cpu_period [get_ports clk]
set_clock_uncertainty 0.200 [get_clocks cpu_clk]

opt_design -directive Explore
place_design -directive ExtraPostPlacementOpt
write_checkpoint -force [file join $checkpoint_dir placed_raw.dcp]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks cpu_clk] \
    -file [file join $report_dir timing_cpu_raw_place.rpt]
if {[info exists ::env(TRIBE_STOP_AFTER_PLACE)] && $::env(TRIBE_STOP_AFTER_PLACE) ne ""} {
    puts "TRIBE_CPU_RAW_PLACE_DONE"
    exit
}
phys_opt_design -directive AggressiveExplore
write_checkpoint -force [file join $checkpoint_dir post_place.dcp]
route_design -directive AggressiveExplore
write_checkpoint -force [file join $checkpoint_dir post_route.dcp]

report_utilization -hierarchical -hierarchical_depth 4 \
    -file [file join $report_dir utilization_post_route.rpt]
report_timing_summary -delay_type min_max -max_paths 100 -report_unconstrained \
    -file [file join $report_dir timing_summary_post_route.rpt]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks cpu_clk] \
    -file [file join $report_dir timing_cpu_to_cpu.rpt]
report_methodology -file [file join $report_dir methodology_post_route.rpt]

puts [format "TRIBE_CPU_DONE period=%.6fns" $cpu_period]
