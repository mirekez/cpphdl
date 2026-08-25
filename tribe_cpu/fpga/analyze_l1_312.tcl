# Standalone physical timing analysis for a parameterized L1 cache.

proc env_or_default {name fallback} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return $::env($name)
    }
    return $fallback
}

set script_dir [file dirname [file normalize [info script]]]
set rtl_dir [file normalize [env_or_default TRIBE_RTL_DIR \
    [file join $script_dir cpphdl_tribe256_multicore generated]]]
set run_dir [file normalize [env_or_default TRIBE_RUN_DIR \
    [file join $script_dir vivado_l1_312]]]
set report_dir [file join $run_dir reports]
set checkpoint_dir [file join $run_dir checkpoints]
set part [env_or_default TRIBE_PART xc7k325tffg676-3]
set cpu_period [env_or_default TRIBE_CPU_PERIOD_NS [expr {1000.0 / 312.0}]]
set cache_size [env_or_default TRIBE_L1_SIZE 2048]
set cache_line_size [env_or_default TRIBE_L1_LINE_SIZE 32]
set ways [env_or_default TRIBE_L1_WAYS 2]
set dcache [env_or_default TRIBE_L1_DCACHE 0]
set addr_bits [env_or_default TRIBE_L1_ADDR_BITS 32]
set port_bits [env_or_default TRIBE_L1_PORT_BITS 256]
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
read_verilog -sv [list \
    [file join $rtl_dir RAM.sv] \
    [file join $rtl_dir L1Cache.sv]]

synth_design -mode out_of_context -top L1Cache -part $part \
    -generic TOTAL_CACHE_SIZE=$cache_size \
    -generic CACHE_LINE_SIZE=$cache_line_size \
    -generic WAYS=$ways \
    -generic DCACHE=$dcache \
    -generic ADDR_BITS=$addr_bits \
    -generic PORT_BITWIDTH=$port_bits \
    -flatten_hierarchy rebuilt

create_clock -name cpu_clk -period $cpu_period [get_ports clk]
set_clock_uncertainty 0.100 [get_clocks cpu_clk]
opt_design -directive Explore
place_design -directive ExtraPostPlacementOpt
write_checkpoint -force [file join $checkpoint_dir post_place.dcp]
report_timing_summary -delay_type min_max -max_paths 100 -report_unconstrained \
    -file [file join $report_dir timing_summary_post_place.rpt]
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
check_timing -verbose -loop_limit 1000 \
    -file [file join $report_dir check_timing_post_route.rpt]
report_high_fanout_nets -timing -max_nets 100 \
    -file [file join $report_dir high_fanout_post_route.rpt]

puts [format "TRIBE_L1_DONE period=%.6fns size=%s ways=%s dcache=%s" \
    $cpu_period $cache_size $ways $dcache]
