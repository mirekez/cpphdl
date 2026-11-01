#pragma once
#include "qualified_frame_crossing.hpp"

// The two PHY receivers and transmitters need not share a clock phase or rate.
// Cross A's recovered frames into B's transmit domain, and B's into A's.
class RepeaterForwarding : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) hold_a_in;
    _PORT(bool) hold_b_in;
    _PORT(bool) idle_a_out = _ASSIGN(b_to_a.idle_out());
    _PORT(bool) idle_b_out = _ASSIGN(a_to_b.idle_out());
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
    _PORT(u<10>) tx_a_out = _ASSIGN(b_to_a.transmit_out());
    _PORT(u<10>) tx_b_out = _ASSIGN(a_to_b.transmit_out());
    _PORT(bool) source_fault_a_out = _ASSIGN(a_to_b.source_fault_out());
    _PORT(bool) source_fault_b_out = _ASSIGN(b_to_a.source_fault_out());
    _PORT(bool) tx_fault_a_out = _ASSIGN(b_to_a.fault_out());
    _PORT(bool) tx_fault_b_out = _ASSIGN(a_to_b.fault_out());
    _PORT(bool) reset_a_rx_out = _ASSIGN(a_to_b.reset_rx_out());
    _PORT(bool) reset_b_rx_out = _ASSIGN(b_to_a.reset_rx_out());
    _PORT(bool) reset_a_tx_out = _ASSIGN(b_to_a.reset_tx_out());
    _PORT(bool) reset_b_tx_out = _ASSIGN(a_to_b.reset_tx_out());
    _PORT(bool) pause_a_out = _ASSIGN(a_to_b.pause_request_out());
    _PORT(bool) pause_b_out = _ASSIGN(b_to_a.pause_request_out());

private:
    QualifiedFrameCrossing a_to_b, b_to_a;

public:
    void _assign() {
        a_to_b.hold_transmit_in = _ASSIGN(hold_b_in());
        b_to_a.hold_transmit_in = _ASSIGN(hold_a_in());
        a_to_b.enable_in = _ASSIGN(enable_in());
        a_to_b.receive_in = _ASSIGN(rx_a_in());
        a_to_b.valid_in = _ASSIGN(valid_a_in());
        a_to_b.fault_in = _ASSIGN(fault_a_in());
        a_to_b.receive_ready_in = _ASSIGN(rx_a_ready_in());
        a_to_b.transmit_ready_in = _ASSIGN(tx_b_ready_in());
        b_to_a.enable_in = _ASSIGN(enable_in());
        b_to_a.receive_in = _ASSIGN(rx_b_in());
        b_to_a.valid_in = _ASSIGN(valid_b_in());
        b_to_a.fault_in = _ASSIGN(fault_b_in());
        b_to_a.receive_ready_in = _ASSIGN(rx_b_ready_in());
        b_to_a.transmit_ready_in = _ASSIGN(tx_a_ready_in());
        a_to_b._assign();
        b_to_a._assign();
    }
    void _work_rx_clk(bool reset) { a_to_b._work_rx_clk(reset); }
    void _work_b_rx_clk(bool reset) { b_to_a._work_rx_clk(reset); }
    void _work_a_tx_clk(bool reset) { b_to_a._work_tx_clk(reset); }
    void _work_tx_clk(bool reset) { a_to_b._work_tx_clk(reset); }
    void _strobe_rx_clk() { a_to_b._strobe_rx_clk(); }
    void _strobe_b_rx_clk() { b_to_a._strobe_rx_clk(); }
    void _strobe_a_tx_clk() { b_to_a._strobe_tx_clk(); }
    void _strobe_tx_clk() { a_to_b._strobe_tx_clk(); }
    void _work_neg_rx_clk(bool) {}
    void _work_neg_b_rx_clk(bool) {}
    void _work_neg_a_tx_clk(bool) {}
    void _work_neg_tx_clk(bool) {}
    void _strobe_neg_rx_clk() {}
    void _strobe_neg_b_rx_clk() {}
    void _strobe_neg_a_tx_clk() {}
    void _strobe_neg_tx_clk() {}
#ifndef SYNTHESIS
    void sim_assert_a_to_b_reset() { a_to_b.sim_assert_reset(); }
    void sim_assert_b_to_a_reset() { b_to_a.sim_assert_reset(); }
#endif
};
