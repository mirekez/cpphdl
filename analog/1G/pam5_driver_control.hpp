#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Code 0 means +2 and code 4 means -2. A P-side high gate sinks that unit
// current from P; the complementary N gates preserve constant total current.
// A separate four-cell bank supplies the preceding symbol's weighted current.
class PAM5DriverControl : public Module {
public:
    _PORT(u<3>) symbol_in;
    _PORT(bool) enable_in;
    _PORT(u<4>) main_p_out = _ASSIGN_REG(main_p);
    _PORT(u<4>) main_n_out = _ASSIGN_REG(main_n);
    _PORT(u<4>) history_p_out = _ASSIGN_REG(history_p);
    _PORT(u<4>) history_n_out = _ASSIGN_REG(history_n);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) fault_out = _ASSIGN_REG(fault);

private:
    reg<u<3>> previous{};
    reg<u<4>> main_p{}, main_n{}, history_p{}, history_n{};
    reg<u1> valid{}, fault{};

public:
    void _assign() {}
    void _work(bool reset) {
        unsigned code, current_mask, previous_mask;
        code = unsigned(symbol_in());
        current_mask = (1u << code) - 1;
        previous_mask = (1u << unsigned(previous)) - 1;
        valid._next = 0;
        if (enable_in() && !fault && code <= 4) {
            main_p._next = current_mask;
            main_n._next = current_mask ^ 15;
            history_p._next = previous_mask;
            history_n._next = previous_mask ^ 15;
            previous._next = code;
            valid._next = 1;
        } else {
            main_p._next = 0;
            main_n._next = 0;
            history_p._next = 0;
            history_n._next = 0;
            previous._next = 2;
            if (enable_in() && code > 4)
                fault._next = 1;
        }
        if (reset) {
            previous._next = 2;
            main_p._next = 0;
            main_n._next = 0;
            history_p._next = 0;
            history_n._next = 0;
            valid._next = 0;
            fault._next = 0;
        }
    }
    void _strobe() {
        previous.strobe();
        main_p.strobe();
        main_n.strobe();
        history_p.strobe();
        history_n.strobe();
        valid.strobe();
        fault.strobe();
    }
};
