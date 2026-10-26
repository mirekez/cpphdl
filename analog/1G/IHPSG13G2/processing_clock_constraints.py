"""Local timing contract for the two-domain processing-clock controller macro."""
import math


def constraints(reference_period_ns=8, acquisition_period_ns=1.25,
                uncertainty_ns=.1, io_delay_ns=.4, ready_output_load_pf=.005):
    values = (reference_period_ns, acquisition_period_ns, uncertainty_ns, io_delay_ns, ready_output_load_pf)
    if any(isinstance(v, bool) or not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError('invalid processing-clock timing contract')
    return f'''create_clock -name reference -period {reference_period_ns} [get_ports ref_clk]
create_clock -name acquisition -period {acquisition_period_ns} [get_ports pcs_clk]
set_clock_uncertainty {uncertainty_ns} [all_clocks]
# This macro result covers intra-domain setup/hold. Gray-bus skew, synchronizer
# placement and reset release are separate CDC integration obligations.
set_clock_groups -asynchronous -group reference -group acquisition
set_input_delay {io_delay_ns} -clock reference [get_ports {{reset enable_in}}]
set_input_transition 0.1 [all_inputs]
set_output_delay {io_delay_ns} -clock reference [get_ports {{coarse_n_out* fine_n_out* clear_out ready_ref_out fault_out measured_edges_out*}}]
set_output_delay {io_delay_ns} -clock acquisition [get_ports ready_pcs_out]
set_load 0.005 [all_outputs]
set_load {ready_output_load_pf} [get_ports ready_pcs_out]
'''
