#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// The digital forwarding core between two complete, clock-aligned 1000BASE-T PHYs.
// One 125 MHz reference clocks both directions. A PHY must align its receive words
// to this clock internally; a recovered-clock GMII must not be wired here directly.
class RepeaterCore : public Module {
public:
    // Bits 7:0 are data, bit 8 is valid, bit 9 is the error indication.
    _PORT(u<10>) rx_a_in;
    _PORT(u<10>) rx_b_in;
    _PORT(bool) link_a_in;
    _PORT(bool) link_b_in;
    _PORT(bool) enable_in;
    _PORT(u<10>) tx_a_out = _ASSIGN_REG(tx_a_reg);
    _PORT(u<10>) tx_b_out = _ASSIGN_REG(tx_b_reg);
    _PORT(bool) active_out = _ASSIGN_REG(active_reg);

private:
    reg<u<10>> tx_a_reg;
    reg<u<10>> tx_b_reg;
    reg<u1> active_reg;

public:
    void _assign() {}

    void _work(bool reset) {
        bool forward = enable_in() && link_a_in() && link_b_in();
        active_reg._next = forward;
        tx_a_reg._next = 0;
        tx_b_reg._next = 0;
        if (forward) {
            // Preserve erroneous frames as errors; suppress out-of-frame receive errors.
            // Idle cycles, preambles, frame bytes and IFG lengths retain their order.
            if ((rx_b_in() & 0x100) != 0)
                tx_a_reg._next = rx_b_in();
            if ((rx_a_in() & 0x100) != 0)
                tx_b_reg._next = rx_a_in();
        }
        if (reset) {
            tx_a_reg.clr();
            tx_b_reg.clr();
            active_reg.clr();
        }
    }

    void _strobe() {
        tx_a_reg.strobe();
        tx_b_reg.strobe();
        active_reg.strobe();
    }
};
