# Keep input permutations as a separate stage with its own exact truth-table
# proof. The caller must rebuild routes/RC and recheck setup, hold and loading.
proc receiver_pin_repair {work} {
    if {[[ord::get_db_block] getName] != "PCSReceiver"} { error "receiver-only pin repair" }
    write_verilog "$work/before_pins.v"
    repair_timing -setup -sequence swap -setup_margin 0.35 -max_iterations 500 -max_passes 50 \
        -skip_last_gasp -skip_vt_swap -skip_crit_vt_swap -verbose
    write_verilog "$work/after_pins.v"
}
