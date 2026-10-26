#pragma once
#include "ethernet_pause_filter.hpp"
#include "ethernet_pause_crossing.hpp"
#include "ethernet_pause_arbiter.hpp"
#include "frame_crossing_reset.hpp"

// One full-duplex MAC-control port with independent RX processing and TX clocks.
// Negotiated symmetric flow control is a prerequisite of the lossless datapath.
class EthernetPausePort : public Module {
public:
    _PORT(u<10>) receive_in;
    _PORT(bool) valid_in;
    _PORT(u<10>) data_in;
    _PORT(bool) idle_in;
    _PORT(bool) pause_request_in;
    _PORT(bool) enable_in;
    _PORT(bool) receive_ready_in;
    _PORT(bool) transmit_ready_in;
    _PORT(u<48>) address_in;
    _PORT(u<10>) receive_out = _ASSIGN(filter.gmii_out());
    _PORT(bool) valid_out = _ASSIGN(!resets.reset_rx_out() && filter.valid_out());
    _PORT(u<10>) transmit_out = _ASSIGN(resets.reset_tx_out() ? u<10>(0) : arbiter.gmii_out());
    _PORT(bool) hold_out = _ASSIGN(resets.reset_tx_out() || arbiter.hold_data_out());
    _PORT(bool) sent_out = _ASSIGN(!resets.reset_tx_out() && arbiter.sent_out());
private:
    FrameCrossingReset resets;
    EthernetPauseFilter filter;
    EthernetPauseCrossing commands;
    EthernetPauseArbiter arbiter;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> request_first{}, request_second{};
    _LAZY_COMB(withdraw, bool)
        return withdraw = !enable_in() || !receive_ready_in() || !transmit_ready_in();
    }
public:
    void _assign() {
        filter.gmii_in = _ASSIGN(receive_in());
        filter.valid_in = _ASSIGN(valid_in());
        filter.enable_in = _ASSIGN(enable_in());
        filter.local_address_in = _ASSIGN(address_in());
        commands.command_in = _ASSIGN(filter.command_out());
        commands.quanta_in = _ASSIGN(filter.quanta_out());
        commands.clear_rx_in = _ASSIGN(resets.reset_rx_out());
        commands.clear_tx_in = _ASSIGN(resets.reset_tx_out());
        arbiter.data_in = _ASSIGN(data_in());
        arbiter.idle_in = _ASSIGN(idle_in());
        arbiter.request_in = _ASSIGN(bool(request_second));
        arbiter.enable_in = _ASSIGN(enable_in());
        arbiter.command_in = _ASSIGN(commands.command_out());
        arbiter.quanta_in = _ASSIGN(commands.quanta_out());
        arbiter.address_in = _ASSIGN(address_in());
        resets._assign(); filter._assign(); commands._assign(); arbiter._assign();
    }
    void _work_rx_clk(bool reset) {
        resets._work_rx_clk(reset || withdraw_func());
        filter._work(reset || resets.reset_rx_out());
        commands._work_rx_clk(reset);
    }
    void _work_tx_clk(bool reset) {
        request_first._next = pause_request_in();
        request_second._next = request_first;
        if (reset || resets.reset_tx_out()) {
            request_first._next = 0; request_second._next = 0;
        }
        resets._work_tx_clk(reset || withdraw_func());
        commands._work_tx_clk(reset);
        arbiter._work(reset || resets.reset_tx_out());
    }
    void _strobe_rx_clk() {
        resets._strobe_rx_clk(); filter._strobe(); commands._strobe_rx_clk();
    }
    void _strobe_tx_clk() {
        resets._strobe_tx_clk(); commands._strobe_tx_clk(); arbiter._strobe();
        request_first.strobe(); request_second.strobe();
    }
    void _work_b_rx_clk(bool) {}
    void _strobe_b_rx_clk() {}
    void _work_a_tx_clk(bool) {}
    void _strobe_a_tx_clk() {}
#ifndef SYNTHESIS
    void sim_assert_reset() { resets._reset_pos_rx_clk(); resets._reset_pos_tx_clk(); }
#endif
};
