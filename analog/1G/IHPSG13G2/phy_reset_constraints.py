"""Local timing and load contract for the three-domain reset controller."""
import math


def constraints(reference_period_ns=8, adc_period_ns=4, pcs_period_ns=1000 / 136,
                uncertainty_ns=.1, io_delay_ns=.4, output_load_pf=.08):
    values = (reference_period_ns, adc_period_ns, pcs_period_ns,
              uncertainty_ns, io_delay_ns, output_load_pf)
    if any(isinstance(v, bool) or not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError('invalid PHY reset timing contract')
    if uncertainty_ns + io_delay_ns >= min(reference_period_ns, adc_period_ns, pcs_period_ns) / 2:
        raise ValueError('PHY reset timing budget exceeds a half-cycle')
    return f'''create_clock -name reference -period {reference_period_ns} [get_ports ref_clk]
create_clock -name adc -period {adc_period_ns} [get_ports adc_clk]
create_clock -name processing -period {pcs_period_ns} [get_ports pcs_clk]
set_clock_uncertainty {uncertainty_ns} [all_clocks]
set_clock_groups -asynchronous -group reference -group adc -group processing
# Qualification may withdraw asynchronously. This local macro verifies the
# release pipeline and output drive; integration must check reset arrival,
# recovery/removal at every downstream sink and synchronizer MTBF separately.
set_input_delay {io_delay_ns} -clock reference [get_ports {{reset enable_in power_good_in adc_ready_in pcs_ready_in}}]
set_input_transition 0.1 [all_inputs]
set_output_delay {io_delay_ns} -clock reference [get_ports reset_ref_out]
set_output_delay {io_delay_ns} -clock adc [get_ports reset_adc_out]
set_output_delay {io_delay_ns} -clock processing [get_ports reset_pcs_out]
set_load {output_load_pf} [all_outputs]
'''
