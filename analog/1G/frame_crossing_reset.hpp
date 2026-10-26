#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// A FIFO may restart only after both clock domains have observed reset.
// Assertion is asynchronous. Release and the peer acknowledgement each use
// two local edges, so a stopped peer keeps both sides of the FIFO held clear.
class FrameCrossingReset : public Module {
public:
    _PORT(bool) reset_rx_out = _ASSIGN(!rx_released || !tx_seen2);
    _PORT(bool) reset_tx_out = _ASSIGN(!tx_released || !rx_seen2);

private:
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> rx_first{}, rx_released{}, tx_seen1{}, tx_seen2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> tx_first{}, tx_released{}, rx_seen1{}, rx_seen2{};

public:
    void _assign() {}
    void _work_rx_clk(bool) {
        rx_first._next = 1;
        rx_released._next = rx_first;
        tx_seen1._next = tx_released;
        tx_seen2._next = tx_seen1;
    }
    void _work_tx_clk(bool) {
        tx_first._next = 1;
        tx_released._next = tx_first;
        rx_seen1._next = rx_released;
        rx_seen2._next = rx_seen1;
    }
    void _reset_pos_rx_clk() {
        rx_first._next = 0;
        rx_released._next = 0;
        tx_seen1._next = 0;
        tx_seen2._next = 0;
    }
    void _reset_pos_tx_clk() {
        tx_first._next = 0;
        tx_released._next = 0;
        rx_seen1._next = 0;
        rx_seen2._next = 0;
    }
    void _strobe_rx_clk() {
        rx_first.strobe();
        rx_released.strobe();
        tx_seen1.strobe();
        tx_seen2.strobe();
    }
    void _strobe_tx_clk() {
        tx_first.strobe();
        tx_released.strobe();
        rx_seen1.strobe();
        rx_seen2.strobe();
    }
    void _work_neg_rx_clk(bool) {}
    void _work_neg_tx_clk(bool) {}
    void _strobe_neg_rx_clk() {}
    void _strobe_neg_tx_clk() {}
    // C++HDL's declared clocks are design-global. These belong to the peer
    // crossing; this instance has state only on rx_clk and tx_clk.
    void _work_b_rx_clk(bool) {}
    void _strobe_b_rx_clk() {}
    void _work_a_tx_clk(bool) {}
    void _strobe_a_tx_clk() {}
};
