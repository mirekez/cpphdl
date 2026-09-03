# TribeTest<4> timing constraints for a phase-aligned 312/156 MHz clock pair.
#
# In the real SmartNIC top level, drive both clocks from the same MMCM/PLL and
# put the generated clock on the actual MMCM/BUFG output pin.  OOC has no
# logical clock-generation path, so the two aligned waveforms are declared here.

# OOC has two unrelated input ports, so declare both waveforms directly.  The
# real board XDC should instead create l2_clk as -divide_by 2 on the MMCM/PLL
# output that actually drives it; do not fake that relation between unconnected
# OOC ports because Vivado correctly reports that there is no logical path.
create_clock -name cpu_clk -period 3.205 -waveform {0.000 1.602} [get_ports clk]
create_clock -name l2_clk -period 6.410 -waveform {0.000 3.205} \
    [get_ports l2_clock]
set_clock_uncertainty 0.100 [get_clocks cpu_clk]
set_clock_uncertainty 0.100 [get_clocks l2_clk]

# The CppHDL CDCs are toggle mailboxes.  Only the D pin of synchronizer stage 1
# is an asynchronous sampling point; stage 2 must remain timed in its receiving
# domain.  ASYNC_REG is already emitted by CppHDL on both stages.
set cdc_stage1_cells [get_cells -hierarchical -quiet -filter \
    {ASYNC_REG == TRUE && NAME =~ *1_reg*}]
set_false_path -to [get_pins -of_objects $cdc_stage1_cells -filter \
    {REF_PIN_NAME == D}]

# Request and response payload registers stay unchanged until their toggle has
# crossed two synchronizer stages and the receiver accepts it.  Do not require
# those bundled payload buses to meet the coincident-edge setup check, but keep
# a finite datapath bound so placement cannot scatter the mailbox bits.  The
# CPU request payload has at least two complete L2 periods before use; the L2
# response payload has two CPU periods before the fast side can consume it.
# report_cdc must still be reviewed to ensure there are no non-mailbox crossings.
set_max_delay -datapath_only 12.820 \
    -from [get_clocks cpu_clk] -to [get_clocks l2_clk]
set_max_delay -datapath_only 6.410 \
    -from [get_clocks l2_clk] -to [get_clocks cpu_clk]

# Do not use set_clock_groups -asynchronous here.  It would override the useful
# physical bounds above and hide any accidental direct CPU/L2 crossing.
