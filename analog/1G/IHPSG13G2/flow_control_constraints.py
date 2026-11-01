"""Intra-domain timing contract for the four-clock flow-control macro."""


def constraints():
    text = ''
    for name, period in [('rx_clk', 1000 / 136), ('b_rx_clk', 1000 / 136),
                         ('a_tx_clk', 8), ('tx_clk', 8)]:
        text += f'create_clock -name {name} -period {period:.12g} [get_ports {name}]\n'
    text += '''set_clock_uncertainty 0.1 [all_clocks]
# Cross-domain Gray buses, command payloads and reset release require separate
# extracted CDC checks. This file qualifies only synchronous intra-domain paths.
set_clock_groups -asynchronous -group rx_clk -group b_rx_clk -group a_tx_clk -group tx_clk
set_input_transition 0.1 [all_inputs]
set_input_delay 0.4 -clock rx_clk [get_ports {enable_in reset pause_a_enabled_in address_a_in* rx_a_in* valid_a_in fault_a_in rx_a_ready_in}]
set_input_delay 0.4 -clock b_rx_clk [get_ports {pause_b_enabled_in address_b_in* rx_b_in* valid_b_in fault_b_in rx_b_ready_in}]
set_input_delay 0.4 -clock a_tx_clk [get_ports tx_a_ready_in]
set_input_delay 0.4 -clock tx_clk [get_ports tx_b_ready_in]
set_output_delay 0.4 -clock a_tx_clk [get_ports {tx_a_out* control_a_sent_out}]
set_output_delay 0.4 -clock tx_clk [get_ports {tx_b_out* control_b_sent_out}]
set_output_delay 0.4 -clock rx_clk [get_ports pause_a_out]
set_output_delay 0.4 -clock b_rx_clk [get_ports pause_b_out]
# fault_out is an asynchronous aggregate for the link/reset controller.
set_false_path -to [get_ports fault_out]
set_load 0.005 [all_outputs]
'''
    return text
