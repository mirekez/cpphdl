#pragma once
#include "repeater_forwarding.hpp"
#include "ethernet_pause_port.hpp"

// Two full-duplex Ethernet ports. RX_A uses rx_clk; TX_B uses tx_clk;
// RX_B uses b_rx_clk; TX_A uses a_tx_clk. Addresses are stable per-port MAC
// identities supplied by chip configuration before enabling the datapath.
class FlowControlledRepeater : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) pause_a_enabled_in;
    _PORT(bool) pause_b_enabled_in;
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
    _PORT(u<10>) tx_a_out = _ASSIGN(a.transmit_out());
    _PORT(u<10>) tx_b_out = _ASSIGN(b.transmit_out());
    _PORT(bool) pause_a_out = _ASSIGN(forwarding.pause_a_out());
    _PORT(bool) pause_b_out = _ASSIGN(forwarding.pause_b_out());
    _PORT(bool) control_a_sent_out = _ASSIGN(a.sent_out());
    _PORT(bool) control_b_sent_out = _ASSIGN(b.sent_out());
    _PORT(bool) fault_out = _ASSIGN(forwarding.source_fault_a_out() || forwarding.source_fault_b_out() ||
                                   forwarding.tx_fault_a_out() || forwarding.tx_fault_b_out());
private:
    RepeaterForwarding forwarding;
    EthernetPausePort a, b;
    _LAZY_COMB(enabled, bool)
        return enabled = enable_in() && pause_a_enabled_in() && pause_b_enabled_in();
    }
public:
    void _assign() {
        a.receive_in = _ASSIGN(rx_a_in()); a.valid_in = _ASSIGN(valid_a_in());
        b.receive_in = _ASSIGN(rx_b_in()); b.valid_in = _ASSIGN(valid_b_in());
        a.address_in = _ASSIGN(address_a_in()); b.address_in = _ASSIGN(address_b_in());
        a.enable_in = _ASSIGN(enabled_func()); b.enable_in = _ASSIGN(enabled_func());
        a.receive_ready_in = _ASSIGN(rx_a_ready_in()); a.transmit_ready_in = _ASSIGN(tx_a_ready_in());
        b.receive_ready_in = _ASSIGN(rx_b_ready_in()); b.transmit_ready_in = _ASSIGN(tx_b_ready_in());
        a.data_in = _ASSIGN(forwarding.tx_a_out()); b.data_in = _ASSIGN(forwarding.tx_b_out());
        a.idle_in = _ASSIGN(forwarding.idle_a_out()); b.idle_in = _ASSIGN(forwarding.idle_b_out());
        a.pause_request_in = _ASSIGN(forwarding.pause_a_out());
        b.pause_request_in = _ASSIGN(forwarding.pause_b_out());
        forwarding.hold_a_in = _ASSIGN(a.hold_out()); forwarding.hold_b_in = _ASSIGN(b.hold_out());
        forwarding.enable_in = _ASSIGN(enabled_func());
        forwarding.rx_a_in = _ASSIGN(a.receive_out()); forwarding.valid_a_in = _ASSIGN(a.valid_out());
        forwarding.rx_b_in = _ASSIGN(b.receive_out()); forwarding.valid_b_in = _ASSIGN(b.valid_out());
        forwarding.fault_a_in = _ASSIGN(fault_a_in()); forwarding.fault_b_in = _ASSIGN(fault_b_in());
        forwarding.rx_a_ready_in = _ASSIGN(rx_a_ready_in()); forwarding.rx_b_ready_in = _ASSIGN(rx_b_ready_in());
        forwarding.tx_a_ready_in = _ASSIGN(tx_a_ready_in()); forwarding.tx_b_ready_in = _ASSIGN(tx_b_ready_in());
        a._assign(); b._assign(); forwarding._assign();
    }
    void _work_rx_clk(bool reset) { a._work_rx_clk(reset); forwarding._work_rx_clk(reset); }
    void _work_b_rx_clk(bool reset) { b._work_rx_clk(reset); forwarding._work_b_rx_clk(reset); }
    void _work_a_tx_clk(bool reset) { a._work_tx_clk(reset); forwarding._work_a_tx_clk(reset); }
    void _work_tx_clk(bool reset) { b._work_tx_clk(reset); forwarding._work_tx_clk(reset); }
    void _strobe_rx_clk() { a._strobe_rx_clk(); forwarding._strobe_rx_clk(); }
    void _strobe_b_rx_clk() { b._strobe_rx_clk(); forwarding._strobe_b_rx_clk(); }
    void _strobe_a_tx_clk() { a._strobe_tx_clk(); forwarding._strobe_a_tx_clk(); }
    void _strobe_tx_clk() { b._strobe_tx_clk(); forwarding._strobe_tx_clk(); }
#ifndef SYNTHESIS
    void sim_assert_reset() {
        a.sim_assert_reset(); b.sim_assert_reset();
        forwarding.sim_assert_a_to_b_reset(); forwarding.sim_assert_b_to_a_reset();
    }
#endif
};
