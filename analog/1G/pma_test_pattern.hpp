#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// One lane's Clause 40 test symbols. The parent supplies the selected 125 MHz
// transmit clock and routes these symbols through its normal driver/history.
class PMATestPattern : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(u<3>) mode_in;
    _PORT(u<11>) seed_in;
    _PORT(bool) restart_in;
    _PORT(u<3>) symbol_out;
    _PORT(bool) valid_out;
    _PORT(bool) slave_clock_out;
    _PORT(bool) fault_out;

private:
    reg<u<11>> state{};
    reg<u<3>> previous_mode{}, symbol{};
    reg<u1> valid{}, slave{}, fault{};

public:
    void _assign() {
        symbol_out = _ASSIGN_REG(symbol);
        valid_out = _ASSIGN_REG(valid);
        slave_clock_out = _ASSIGN_REG(slave);
        fault_out = _ASSIGN_REG(fault);
    }
    void _work(bool reset) {
        unsigned mode, current, code, selector, feedback;
        bool restart, failed;
        mode = unsigned(mode_in());
        restart = restart_in() || mode != unsigned(previous_mode);
        current = restart ? (mode == 4 ? unsigned(seed_in()) : 0) : unsigned(state);
        failed = restart ? false : bool(fault);
        code = 2;
        selector = 0;
        feedback = 0;
        previous_mode._next = mode;
        valid._next = 0;
        slave._next = 0;
        if (mode >= 1 && mode <= 4 && !failed) {
            if (mode == 4 && current == 0) {
                failed = true;
            } else {
                if (mode == 1) {
                    if (current == 0)
                        code = 4;
                    else if (current == 128)
                        code = 0;
                    else if (current == 256)
                        code = 3;
                    else if (current == 384)
                        code = 1;
                    else if (current >= 512 && current < 1024)
                        code = (current & 128) == 0 ? 4 : 0;
                } else if (mode == 2 || mode == 3) {
                    code = (current & 1) == 0 ? 4 : 0;
                } else {
                    selector = (current & 1) | (((current >> 1) ^ (current >> 4)) & 1) << 1 |
                               (((current >> 2) ^ (current >> 4)) & 1) << 2;
                    switch (selector) {
                    case 0: case 4: code = 2; break;
                    case 1: case 5: code = 3; break;
                    case 2: code = 4; break;
                    case 3: case 7: code = 1; break;
                    case 6: code = 0; break;
                    }
                    feedback = ((current >> 10) ^ (current >> 8)) & 1;
                }
                state._next = mode == 4 ? (current << 1) | feedback : current + 1;
                valid._next = 1;
                // This requests slave timing; it does not switch a physical clock.
                slave._next = mode == 3;
            }
        } else if (mode > 4) {
            failed = true;
        }
        symbol._next = code;
        fault._next = failed;
        if (reset || !enable_in() || mode == 0) {
            state._next = 0;
            previous_mode._next = 0;
            symbol._next = 2;
            valid._next = 0;
            slave._next = 0;
            fault._next = 0;
        }
    }
    void _strobe() {
        state.strobe();
        previous_mode.strobe();
        symbol.strobe();
        valid.strobe();
        slave.strobe();
        fault.strobe();
    }
};
