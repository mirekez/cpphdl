# Register-to-register physical timing analysis for the combinational Execute.

set script_dir [file dirname [file normalize [info script]]]
set rtl_dir [file join $script_dir cpphdl_tribe256_multicore generated]
set run_dir [file join $script_dir vivado_execute_312]
set report_dir [file join $run_dir reports]
set checkpoint_dir [file join $run_dir checkpoints]
set part xc7k325tffg676-3
set cpu_period [expr {1000.0 / 312.0}]
file mkdir $report_dir
file mkdir $checkpoint_dir

read_verilog -sv [list \
    [file join $rtl_dir Predef_pkg.sv] \
    [file join $rtl_dir Alu_pkg.sv] \
    [file join $rtl_dir Amo_pkg.sv] \
    [file join $rtl_dir Br_pkg.sv] \
    [file join $rtl_dir Csr_pkg.sv] \
    [file join $rtl_dir Mem_pkg.sv] \
    [file join $rtl_dir Sys_pkg.sv] \
    [file join $rtl_dir State_pkg.sv] \
    [file join $rtl_dir Execute.sv] \
    [file join $script_dir execute_timing_top.sv]]

synth_design -mode out_of_context -top ExecuteTimingTop -part $part \
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
report_timing -delay_type max -max_paths 20 -nworst 5 -unique_pins \
    -from [get_clocks cpu_clk] -to [get_clocks cpu_clk] \
    -file [file join $report_dir timing_cpu_to_cpu.rpt]

puts [format "TRIBE_EXECUTE_DONE period=%.6fns" $cpu_period]
