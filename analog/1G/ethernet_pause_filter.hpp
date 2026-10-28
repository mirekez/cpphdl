#pragma once
#include "ethernet_pause_receive.hpp"

// Delay the recovered stream by 32 symbol enables. PAUSE headers are recognized
// before their first delayed byte can escape. Ordinary bytes, RX_ER and idle
// spacing are preserved. A malformed control frame is suppressed but cannot
// change the pause timer, since the parser checks the entire frame and FCS.
class EthernetPauseFilter : public Module {
public:
    _PORT(u<10>) gmii_in;
    _PORT(bool) valid_in;
    _PORT(bool) enable_in;
    _PORT(u<48>) local_address_in;
    _PORT(u<10>) gmii_out = _ASSIGN_REG(output);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) command_out = _ASSIGN(parser.command_out());
    _PORT(u<16>) quanta_out = _ASSIGN(parser.quanta_out());
private:
    EthernetPauseReceive parser;
    memory<u<10>, 1, 32> delay;
    reg<u<5>> position{};
    reg<u<6>> filled{}, tail{};
    reg<u<10>> output{};
    reg<u1> valid{};
public:
    void _assign() {
        parser.gmii_in = _ASSIGN(gmii_in());
        parser.valid_in = _ASSIGN(valid_in());
        parser.enable_in = _ASSIGN(enable_in());
        parser.local_address_in = _ASSIGN(local_address_in());
        parser._assign();
    }
    void _work(bool reset) {
        valid._next = 0;
        if (valid_in()) {
            if (filled == 32) {
                output._next = parser.control_out() || tail != 0 ? u<10>(0) : u<10>(unsigned(delay[unsigned(position)]));
                valid._next = 1;
            } else filled._next = filled + 1;
            delay[unsigned(position)] = gmii_in();
            position._next = position + 1;
            if (tail != 0) tail._next = tail - 1;
            if (parser.control_out()) tail._next = 32;
        }
        parser._work(reset);
        if (reset) {
            position._next = 0; filled._next = 0; tail._next = 0;
            output._next = 0; valid._next = 0;
        }
    }
    void _strobe() {
        delay.apply(); position.strobe(); filled.strobe(); tail.strobe();
        output.strobe(); valid.strobe(); parser._strobe();
    }
};
