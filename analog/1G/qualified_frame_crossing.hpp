#pragma once
#include "frame_crossing_reset.hpp"
#include "synchronous_frame_fifo.hpp"

// Connect a recovered GMII stream to the other port's transmit clock.
// Readiness describes delivered clocks/link state, not a host timing model.
class QualifiedFrameCrossing : public Module {
public:
    _PORT(u<10>) receive_in;
    _PORT(bool) valid_in;
    _PORT(bool) fault_in;
    _PORT(bool) enable_in;
    _PORT(bool) receive_ready_in;
    _PORT(bool) transmit_ready_in;
    _PORT(u<10>) transmit_out = _ASSIGN(resets.reset_tx_out() ? u<10>(0) : crossing.transmit_out());
    _PORT(bool) source_fault_out = _ASSIGN(!resets.reset_rx_out() && crossing.source_fault_out());
    _PORT(bool) fault_out = _ASSIGN(!resets.reset_tx_out() && crossing.fault_out());
    _PORT(bool) reset_rx_out = _ASSIGN(resets.reset_rx_out());
    _PORT(bool) reset_tx_out = _ASSIGN(resets.reset_tx_out());
    _PORT(bool) pause_request_out = _ASSIGN(!resets.reset_rx_out() && pause_request);
    _PORT(bool) hold_transmit_in;
    _PORT(bool) idle_out = _ASSIGN(!resets.reset_tx_out() && crossing.idle_out());

private:
    FrameCrossingReset resets;
    SynchronousFrameFIFO crossing;
    reg<u1> pause_request{};

    _LAZY_COMB(withdraw_comb, bool)
        return withdraw_comb = !enable_in() || !receive_ready_in() || !transmit_ready_in();
    }

public:
    void _assign() {
        crossing.receive_in = _ASSIGN(receive_in());
        crossing.hold_tx_in = _ASSIGN(hold_transmit_in());
        crossing.valid_in = _ASSIGN(valid_in() && !resets.reset_rx_out());
        crossing.fault_in = _ASSIGN(fault_in() && !resets.reset_rx_out());
        crossing.clear_rx_in = _ASSIGN(resets.reset_rx_out());
        crossing.clear_tx_in = _ASSIGN(resets.reset_tx_out());
        resets._assign();
        crossing._assign();
    }
    void _work_rx_clk(bool reset) {
        // Request upstream PAUSE early enough to retain in-flight traffic.
        // Hysteresis avoids repeated pause/resume requests around one level.
        if (crossing.used_out() >= 256) pause_request._next = 1;
        if (crossing.used_out() <= 64) pause_request._next = 0;
        if (reset || resets.reset_rx_out()) pause_request._next = 0;
        resets._work_rx_clk(reset || withdraw_comb_func());
        crossing._work_rx_clk(reset);
    }
    void _work_tx_clk(bool reset) {
        resets._work_tx_clk(reset || withdraw_comb_func());
        crossing._work_tx_clk(reset);
    }
    void _strobe_rx_clk() {
        pause_request.strobe();
        resets._strobe_rx_clk();
        crossing._strobe_rx_clk();
    }
    void _strobe_tx_clk() {
        resets._strobe_tx_clk();
        crossing._strobe_tx_clk();
    }
    void _work_neg_rx_clk(bool) {}
    void _work_neg_tx_clk(bool) {}
    void _strobe_neg_rx_clk() {}
    void _strobe_neg_tx_clk() {}
    void _work_b_rx_clk(bool) {}
    void _strobe_b_rx_clk() {}
    void _work_a_tx_clk(bool) {}
    void _strobe_a_tx_clk() {}
#ifndef SYNTHESIS
    // Dispatch asynchronous assertion even when neither delivered clock runs.
    void sim_assert_reset() {
        resets._reset_pos_rx_clk();
        resets._reset_pos_tx_clk();
    }
#endif
};
