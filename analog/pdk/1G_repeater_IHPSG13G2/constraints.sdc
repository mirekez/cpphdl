# Ref-clock-aligned PHY interfaces. This is the core constraint, not an MDI timing model.
create_clock -name reference -period 8.0 [get_ports ref_clock]
set_clock_uncertainty 0.10 [get_clocks reference]
set_clock_transition 0.10 [get_clocks reference]
set_input_delay 1.0 -clock reference [get_ports {reset rx_a_in* rx_b_in* link_a_in link_b_in enable_in}]
set_output_delay 1.0 -clock reference [all_outputs]
set_input_transition 0.10 [get_ports {reset rx_a_in* rx_b_in* link_a_in link_b_in enable_in}]
set_load 0.006 [all_outputs]
set_max_fanout 8 [current_design]
