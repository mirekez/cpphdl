#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Bundled-data handshake for a validated PAUSE command. The payload remains
// stable until the destination acknowledges it. While the destination clock
// is stopped, retain the latest subsequent command (including a zero resume).
// Domain resets must be coordinated before either side is released.
class EthernetPauseCrossing : public Module {
public:
    _PORT(bool) command_in;
    _PORT(u<16>) quanta_in;
    _PORT(bool) clear_rx_in;
    _PORT(bool) clear_tx_in;
    _PORT(bool) command_out = _ASSIGN_REG(command);
    _PORT(u<16>) quanta_out = _ASSIGN_REG(output);
private:
    reg<u<16>> payload{}, latest{}, output{};
    reg<u1> request{}, acknowledge{}, pending{}, command{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ack_first{}, ack_second{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> request_first{}, request_second{};
public:
    void _assign() {}
    void _work_rx_clk(bool reset) {
        ack_first._next = acknowledge;
        ack_second._next = ack_first;
        if (command_in()) {
            latest._next = quanta_in();
            pending._next = 1;
        }
        if (request == ack_second && (command_in() || pending)) {
            payload._next = command_in() ? quanta_in() : u<16>(latest);
            request._next = !request;
            pending._next = 0;
        }
        if (reset || clear_rx_in()) {
            payload._next = 0; latest._next = 0; pending._next = 0;
            request._next = 0; ack_first._next = 0; ack_second._next = 0;
        }
    }
    void _work_tx_clk(bool reset) {
        request_first._next = request;
        request_second._next = request_first;
        command._next = 0;
        if (request_second != acknowledge) {
            output._next = payload;
            command._next = 1;
            acknowledge._next = request_second;
        }
        if (reset || clear_tx_in()) {
            output._next = 0; command._next = 0; acknowledge._next = 0;
            request_first._next = 0; request_second._next = 0;
        }
    }
    void _strobe_rx_clk() {
        payload.strobe(); latest.strobe(); pending.strobe(); request.strobe();
        ack_first.strobe(); ack_second.strobe();
    }
    void _strobe_tx_clk() {
        output.strobe(); command.strobe(); acknowledge.strobe();
        request_first.strobe(); request_second.strobe();
    }
    void _work_b_rx_clk(bool) {}
    void _strobe_b_rx_clk() {}
    void _work_a_tx_clk(bool) {}
    void _strobe_a_tx_clk() {}
};
