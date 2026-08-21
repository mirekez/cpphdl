# Vivado timing characterization for the generated four-core Tribe cluster.
#
# Environment overrides:
#   TRIBE_CPU_PERIOD_NS  CPU clock period (default 10.0 ns)
#   TRIBE_L2_PERIOD_NS   L2 clock period  (default 20.0 ns)
#   TRIBE_CDC_MODE       "timed" for phase-related clocks, "async" for
#                        independent clock inputs (default timed)
#   TRIBE_SYNTH_ONLY     1 to stop after synthesis/DRC (default 0)
#   TRIBE_RUN_DIR        output directory
#                        (default tribe_cpu/fpga/vivado_tribe_timing)

proc env_or_default {name fallback} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return $::env($name)
    }
    return $fallback
}

set script_dir [file dirname [file normalize [info script]]]
set repo_dir [file dirname [file dirname $script_dir]]
set rtl_dir [file join $script_dir cpphdl_tribe256_multicore generated]
set run_dir [file normalize [env_or_default TRIBE_RUN_DIR \
    [file join $script_dir vivado_tribe_timing]]]
set report_dir [file join $run_dir reports]
set checkpoint_dir [file join $run_dir checkpoints]
set cpu_period [env_or_default TRIBE_CPU_PERIOD_NS 3.205128]
set l2_period [env_or_default TRIBE_L2_PERIOD_NS 6.410256]
set cdc_mode [env_or_default TRIBE_CDC_MODE timed]
set synth_only [env_or_default TRIBE_SYNTH_ONLY 0]
# Vivado's canonical spelling for the requested XC7K325T-3FFG676 device.
# The trailing E is the environmental/temperature ordering suffix, not part of
# the Vivado device identifier.
set part xc7k325tffg676-3

if {![file isdirectory $rtl_dir]} {
    error "Generated RTL is missing: $rtl_dir"
}
if {$cdc_mode ni {timed async}} {
    error "TRIBE_CDC_MODE must be either timed or async"
}

file mkdir $report_dir
file mkdir $checkpoint_dir

# Package order matters because a few generated packed types import other
# generated packages.  All remaining leaf packages can then be read together.
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

# OOC preserves the complete AXI-facing cluster without requiring thousands of
# package pins.  The reports therefore characterize the CPU, L1s, CDCs, and L2,
# rather than an optimizer-pruned constant-input demo wrapper.
synth_design -mode out_of_context -top TribeTest -part $part \
    -generic CPU_CORES=4 -flatten_hierarchy rebuilt

create_clock -name cpu_clk -period $cpu_period [get_ports clk]
create_clock -name l2_clk -period $l2_period [get_ports l2_clock]
set_clock_uncertainty 0.200 [get_clocks cpu_clk]
set_clock_uncertainty 0.200 [get_clocks l2_clk]

# The CppHDL crossings are toggle mailboxes.  Only synchronizer stage 1 is an
# asynchronous sampling point; stage 2 remains normally timed in its receiving
# domain.  The bundled payload registers remain stable until the synchronized
# toggle is observed, so give those buses a finite placement bound matching the
# two-stage protocol instead of forcing a coincident-edge setup check.
set cdc_stage1_cells [get_cells -hierarchical -quiet -filter \
    {ASYNC_REG == TRUE && NAME =~ *1_reg*}]
set_false_path -to [get_pins -of_objects $cdc_stage1_cells -filter \
    {REF_PIN_NAME == D}]

# Use async only when clk and l2_clock are truly separate board inputs.  For
# the documented phase-aligned 2:1 topology, keep CDC_MODE=timed and retain
# bounded payload paths in both directions.  Do not use set_multicycle_path on
# the whole interface: that would also relax accidental non-mailbox crossings.
if {$cdc_mode eq "async"} {
    set_clock_groups -asynchronous \
        -group [get_clocks cpu_clk] -group [get_clocks l2_clk]
} else {
    set_max_delay -datapath_only [expr {2.0 * $l2_period}] \
        -from [get_clocks cpu_clk] -to [get_clocks l2_clk]
    set_max_delay -datapath_only [expr {2.0 * $cpu_period}] \
        -from [get_clocks l2_clk] -to [get_clocks cpu_clk]
}

write_checkpoint -force [file join $checkpoint_dir post_synth.dcp]
report_utilization -hierarchical -hierarchical_depth 4 \
    -file [file join $report_dir utilization_post_synth.rpt]
report_timing_summary -delay_type min_max -max_paths 20 -report_unconstrained \
    -file [file join $report_dir timing_post_synth.rpt]
report_timing -delay_type max -max_paths 50 -nworst 10 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks cpu_clk] \
    -file [file join $report_dir timing_cpu_to_cpu_post_synth.rpt]
report_timing -delay_type max -max_paths 50 -nworst 10 -unique_pins \
    -from [get_clocks l2_clk] -to [get_clocks l2_clk] \
    -file [file join $report_dir timing_l2_to_l2_post_synth.rpt]
report_clock_interaction \
    -file [file join $report_dir clock_interaction_post_synth.rpt]
report_cdc -details \
    -file [file join $report_dir cdc_post_synth.rpt]
report_drc \
    -file [file join $report_dir drc_post_synth.rpt]

if {$synth_only} {
    puts "TRIBE_SYNTH_DONE part=$part cpu_period=$cpu_period l2_period=$l2_period cdc_mode=$cdc_mode"
    exit 0
}

opt_design
place_design
phys_opt_design
route_design
write_checkpoint -force [file join $checkpoint_dir post_route.dcp]

report_utilization -hierarchical -hierarchical_depth 5 \
    -file [file join $report_dir utilization_post_route.rpt]
report_timing_summary -delay_type min_max -max_paths 100 -report_unconstrained \
    -file [file join $report_dir timing_summary_post_route.rpt]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks cpu_clk] \
    -file [file join $report_dir timing_cpu_to_cpu.rpt]
report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
    -from [get_clocks l2_clk] -to [get_clocks l2_clk] \
    -file [file join $report_dir timing_l2_to_l2.rpt]
if {$cdc_mode eq "timed"} {
    report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
        -from [get_clocks cpu_clk] -to [get_clocks l2_clk] \
        -file [file join $report_dir timing_cpu_to_l2.rpt]
    report_timing -delay_type max -max_paths 100 -nworst 20 -unique_pins \
        -from [get_clocks l2_clk] -to [get_clocks cpu_clk] \
        -file [file join $report_dir timing_l2_to_cpu.rpt]
}
report_clock_interaction \
    -file [file join $report_dir clock_interaction_post_route.rpt]
report_cdc -details \
    -file [file join $report_dir cdc_post_route.rpt]
report_methodology \
    -file [file join $report_dir methodology_post_route.rpt]
report_drc \
    -file [file join $report_dir drc_post_route.rpt]

puts "TRIBE_TIMING_DONE part=$part cpu_period=$cpu_period l2_period=$l2_period cdc_mode=$cdc_mode"
