#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Clause 28 legacy sixteen-bit FLP burst timing from one 125 MHz reference.
// A page is captured at the first clock pulse and held for the whole burst.
// The output controls a physical link-pulse driver; it is not an MDI voltage.
class FLPTransmitter : public Module {
public:
    _PORT(u<16>) page_in;
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) pulse_out = _ASSIGN_REG(pulse);
    _PORT(bool) burst_begin_out = _ASSIGN_REG(begin);
    _PORT(bool) burst_done_out = _ASSIGN_REG(done);

private:
    reg<u<21>> phase{};
    reg<u<18>> next_pulse{};
    reg<u<6>> half_slot{};
    reg<u<4>> width{};
    reg<u<16>> page{};
    reg<u1> pulse{}, begin{}, done{};

public:
    void _assign() {}
    void _work(bool reset) {
        begin._next = 0;
        done._next = phase == 250013; // The seventeenth clock pulse has ended.
        pulse._next = width != 0;
        if (width != 0)
            width._next = width - 1;
        phase._next = phase == 1999999 ? 0 : unsigned(phase) + 1;
        if (phase == 0) {
            page._next = page_in();
            begin._next = 1;
            pulse._next = 1;
            width._next = 12; // This edge and twelve more: 104 ns.
            next_pulse._next = 7812;
            half_slot._next = 1;
        } else if (half_slot < 33 && phase == next_pulse) {
            // Alternating 7812/7813-cycle half-slots give exact 125 us clock
            // spacing; data pulses sit 4 ns before the nominal midpoint.
            if ((half_slot & 1) == 0 || ((page >> (unsigned(half_slot) >> 1)) & 1) != 0) {
                pulse._next = 1;
                width._next = 12;
            }
            next_pulse._next = unsigned(next_pulse) + ((half_slot & 1) ? 7813 : 7812);
            half_slot._next = half_slot + 1;
        }
        if (reset || !enable_in() || restart_in()) {
            phase._next = 0;
            next_pulse._next = 0;
            half_slot._next = 0;
            width._next = 0;
            page._next = 0;
            pulse._next = 0;
            begin._next = 0;
            done._next = 0;
        }
    }
    void _strobe() {
        phase.strobe();
        next_pulse.strobe();
        half_slot.strobe();
        width.strobe();
        page.strobe();
        pulse.strobe();
        begin.strobe();
        done.strobe();
    }
};
