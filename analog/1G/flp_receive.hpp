#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Decode legacy FLP timing after a physical pulse detector. The two input
// synchronizers reduce CDC exposure; a routed implementation still needs its
// own metastability/MTBF assessment. No link or negotiated-mode claim is made.
class FLPReceiver : public Module {
public:
    _PORT(bool) pulse_in;
    _PORT(bool) enable_in;
    _PORT(u<16>) page_out = _ASSIGN_REG(page);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) fault_out = _ASSIGN_REG(fault);

private:
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> input_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> input_sync2{};
    reg<u1> previous{};
    reg<u<6>> width{};
    reg<u<15>> age{};
    reg<u<4>> bit{};
    reg<u<16>> assembling{}, page{};
    reg<u1> active{}, data_seen{}, valid{}, fault{};

public:
    void _assign() {}
    void _work(bool reset) {
        input_sync1._next = pulse_in();
        input_sync2._next = input_sync1;
        previous._next = input_sync2;
        valid._next = 0;
        fault._next = 0;
        if (age < 20000)
            age._next = age + 1;
        else if (active) {
            active._next = 0;
            fault._next = 1; // No clock within the 160 us receive timeout.
        }
        if (input_sync2) {
            if (width < 63)
                width._next = width + 1;
        } else {
            width._next = 0;
            if (previous && width > 32) {
                active._next = 0;
                fault._next = 1;
            } else if (previous && width >= 5) {
                // Qualify pulse width at its falling edge. A sub-40 ns glitch
                // cannot supply a clock or set a data bit.
                if (!active || age >= 20000) {
                    active._next = 1;
                    bit._next = 0;
                    assembling._next = 0;
                    data_seen._next = 0;
                    age._next = 0;
                } else if (age >= 5875 && age <= 9750 && !data_seen) {
                    // Data aperture 47..78 us after the last qualified clock.
                    assembling._next = assembling | (1 << unsigned(bit));
                    data_seen._next = 1;
                } else if (age >= 13875 && age <= 17375) {
                    // Clock aperture 111..139 us. Seventeen clocks delimit
                    // sixteen data positions; the final clock commits the page.
                    age._next = 0;
                    data_seen._next = 0;
                    if (bit == 15) {
                        page._next = assembling;
                        valid._next = 1;
                        active._next = 0;
                    } else {
                        bit._next = bit + 1;
                    }
                } else {
                    active._next = 0;
                    fault._next = 1;
                }
            }
        }
        if (reset || !enable_in()) {
            input_sync1._next = 0;
            input_sync2._next = 0;
            previous._next = 0;
            width._next = 0;
            age._next = 20000;
            bit._next = 0;
            assembling._next = 0;
            page._next = 0;
            active._next = 0;
            data_seen._next = 0;
            valid._next = 0;
            fault._next = 0;
        }
    }
    void _strobe() {
        input_sync1.strobe();
        input_sync2.strobe();
        previous.strobe();
        width.strobe();
        age.strobe();
        bit.strobe();
        assembling.strobe();
        page.strobe();
        active.strobe();
        data_seen.strobe();
        valid.strobe();
        fault.strobe();
    }
};
