#pragma once
#include "autonegotiation.hpp"
#include "flow_controlled_repeater.hpp"

// Reference-clock negotiation controls both independently clocked MAC paths.
// The PHY supplies electrical FLP detection, trained-link status and delivered
// RX/TX clocks. Stable configuration is established before enable_in rises.
class NegotiatedRepeater : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) restart_a_in;
    _PORT(bool) restart_b_in;
    _PORT(bool) pulse_a_in;
    _PORT(bool) pulse_b_in;
    _PORT(bool) pma_a_link_in;
    _PORT(bool) pma_b_link_in;
    _PORT(u<11>) configuration_a_in;
    _PORT(u<11>) configuration_b_in;
    _PORT(u<11>) seed_a_in;
    _PORT(u<11>) seed_b_in;
    _PORT(u<48>) address_a_in;
    _PORT(u<48>) address_b_in;
    _PORT(u<10>) rx_a_in;
    _PORT(u<10>) rx_b_in;
    _PORT(bool) valid_a_in;
    _PORT(bool) valid_b_in;
    _PORT(bool) fault_a_in;
    _PORT(bool) fault_b_in;
    _PORT(bool) rx_a_ready_in;
    _PORT(bool) rx_b_ready_in;
    _PORT(bool) tx_a_ready_in;
    _PORT(bool) tx_b_ready_in;
    _PORT(bool) pulse_a_out;
    _PORT(bool) pulse_b_out;
    _PORT(bool) pma_a_enable_out;
    _PORT(bool) pma_b_enable_out;
    _PORT(bool) master_a_out;
    _PORT(bool) master_b_out;
    _PORT(bool) link_a_out;
    _PORT(bool) link_b_out;
    _PORT(bool) pause_a_enabled_out;
    _PORT(bool) pause_b_enabled_out;
    _PORT(bool) admitted_out;
    _PORT(bool) fault_out;
    _PORT(u<10>) tx_a_out;
    _PORT(u<10>) tx_b_out;

private:
    Autonegotiation a, b;
    FlowControlledRepeater flow;
    _LAZY_COMB(enabled, bool)
        return enabled = enable_in() && !restart_a_in() && !restart_b_in() &&
                         pma_a_link_in() && pma_b_link_in();
    }

public:
    void _assign() {
        a.enable_in = _ASSIGN(enable_in());
        b.enable_in = _ASSIGN(enable_in());
        a.restart_in = _ASSIGN(restart_a_in());
        b.restart_in = _ASSIGN(restart_b_in());
        a.pulse_in = _ASSIGN(pulse_a_in());
        b.pulse_in = _ASSIGN(pulse_b_in());
        a.pma_link_in = _ASSIGN(pma_a_link_in());
        b.pma_link_in = _ASSIGN(pma_b_link_in());
        a.local_up1_in = _ASSIGN(configuration_a_in());
        b.local_up1_in = _ASSIGN(configuration_b_in());
        a.entropy_seed_in = _ASSIGN(seed_a_in());
        b.entropy_seed_in = _ASSIGN(seed_b_in());
        pulse_a_out = _ASSIGN(a.pulse_out());
        pulse_b_out = _ASSIGN(b.pulse_out());
        pma_a_enable_out = _ASSIGN(a.pma_enable_out());
        pma_b_enable_out = _ASSIGN(b.pma_enable_out());
        master_a_out = _ASSIGN(a.master_out());
        master_b_out = _ASSIGN(b.master_out());
        link_a_out = _ASSIGN(a.link_out());
        link_b_out = _ASSIGN(b.link_out());
        pause_a_enabled_out = _ASSIGN(a.pause_out());
        pause_b_enabled_out = _ASSIGN(b.pause_out());
        admitted_out = _ASSIGN(enabled_func() && a.pause_out() && b.pause_out());
        flow.enable_in = _ASSIGN(enabled_func());
        flow.pause_a_enabled_in = _ASSIGN(a.pause_out());
        flow.pause_b_enabled_in = _ASSIGN(b.pause_out());
        flow.address_a_in = _ASSIGN(address_a_in());
        flow.address_b_in = _ASSIGN(address_b_in());
        flow.rx_a_in = _ASSIGN(rx_a_in());
        flow.rx_b_in = _ASSIGN(rx_b_in());
        flow.valid_a_in = _ASSIGN(valid_a_in());
        flow.valid_b_in = _ASSIGN(valid_b_in());
        flow.fault_a_in = _ASSIGN(fault_a_in());
        flow.fault_b_in = _ASSIGN(fault_b_in());
        flow.rx_a_ready_in = _ASSIGN(rx_a_ready_in());
        flow.rx_b_ready_in = _ASSIGN(rx_b_ready_in());
        flow.tx_a_ready_in = _ASSIGN(tx_a_ready_in());
        flow.tx_b_ready_in = _ASSIGN(tx_b_ready_in());
        tx_a_out = _ASSIGN(flow.tx_a_out());
        tx_b_out = _ASSIGN(flow.tx_b_out());
        fault_out = _ASSIGN(a.fault_out() || b.fault_out() || flow.fault_out());
        a._assign();
        b._assign();
        flow._assign();
    }
    void _work_ref_clk(bool reset) { a._work(reset); b._work(reset); }
    void _strobe_ref_clk() { a._strobe(); b._strobe(); }
    void _work_rx_clk(bool reset) { flow._work_rx_clk(reset); }
    void _work_b_rx_clk(bool reset) { flow._work_b_rx_clk(reset); }
    void _work_a_tx_clk(bool reset) { flow._work_a_tx_clk(reset); }
    void _work_tx_clk(bool reset) { flow._work_tx_clk(reset); }
    void _strobe_rx_clk() { flow._strobe_rx_clk(); }
    void _strobe_b_rx_clk() { flow._strobe_b_rx_clk(); }
    void _strobe_a_tx_clk() { flow._strobe_a_tx_clk(); }
    void _strobe_tx_clk() { flow._strobe_tx_clk(); }
#ifndef SYNTHESIS
    void sim_assert_reset() { flow.sim_assert_reset(); }
#endif
};
