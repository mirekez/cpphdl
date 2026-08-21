# Standalone physical timing analysis for the shared 64 KiB, four-port L2.

set script_dir [file dirname [file normalize [info script]]]
set rtl_dir [file join $script_dir cpphdl_tribe256_multicore generated]
set run_dir [file join $script_dir vivado_l2_156]
if {[info exists ::env(TRIBE_RUN_DIR)] && $::env(TRIBE_RUN_DIR) ne ""} {
    set run_dir [file normalize $::env(TRIBE_RUN_DIR)]
}
set report_dir [file join $run_dir reports]
set checkpoint_dir [file join $run_dir checkpoints]
set part xc7k325tffg676-3
set l2_period [expr {1000.0 / 156.0}]
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
    CacheRequest_pkg.sv]
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
read_verilog -sv [file join $rtl_dir L2Cache.sv]

synth_design -mode out_of_context -top L2Cache -part $part \
    -generic CACHE_SIZE=65536 \
    -generic PORT_BITWIDTH=256 \
    -generic CACHE_LINE_SIZE=32 \
    -generic WAYS=4 \
    -generic ADDR_BITS=32 \
    -generic MEM_ADDR_BITS=23 \
    -generic MEM_PORTS=4 \
    -generic CPU_PORTS=4 \
    -flatten_hierarchy rebuilt

create_clock -name l2_clk -period $l2_period [get_ports l2_clock]
set_clock_uncertainty 0.100 [get_clocks l2_clk]
opt_design -directive Explore
place_design -directive ExtraPostPlacementOpt
write_checkpoint -force [file join $checkpoint_dir placed_raw.dcp]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks l2_clk] -to [get_clocks l2_clk] \
    -file [file join $report_dir timing_l2_raw_place.rpt]
if {[info exists ::env(TRIBE_STOP_AFTER_PLACE)] && $::env(TRIBE_STOP_AFTER_PLACE) ne ""} {
    puts "TRIBE_L2_RAW_PLACE_DONE"
    exit
}
phys_opt_design -directive AggressiveExplore
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
    -from [get_clocks l2_clk] -to [get_clocks l2_clk] \
    -file [file join $report_dir timing_l2_to_l2.rpt]
report_methodology -file [file join $report_dir methodology_post_route.rpt]

puts [format "TRIBE_L2_DONE period=%.6fns" $l2_period]
