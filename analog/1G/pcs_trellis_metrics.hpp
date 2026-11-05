#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// One symbol-register boundary for metrics and their exact modular differences.
// Constrain metrics BEFORE this stage, so forced termination, reset and pauses
// cannot leave the prepared decoder with differences from another symbol.
class PCSTrellisMetricRegister : public Module {
public:
    _PORT(bool) symbol_enable_in;
    _PORT(u<18>) metric_in[8];
    _PORT(u<18>) metric_out[8];
    _PORT(u<21>) difference_out[24];

private:
    reg<u<18>> metrics[8]{};
    reg<u<21>> differences[24]{};

public:
    void _assign() {
        for (unsigned i = 0; i < 8; ++i)
            metric_out[i] = _ASSIGN_REG_I(metrics[i]);
        for (unsigned i = 0; i < 24; ++i)
            difference_out[i] = _ASSIGN_REG_I(differences[i]);
    }
    void _work(bool reset) {
        unsigned index, s, t;
        index = 0;
        s = 0;
        t = 0;
        if (symbol_enable_in()) {
            for (unsigned i = 0; i < 8; ++i)
                metrics[i]._next = metric_in[i]();
            for (unsigned parity = 0; parity < 2; ++parity) {
                for (unsigned from = 0; from < 4; ++from) {
                    for (unsigned to = 0; to < 4; ++to) {
                        if (from != to) {
                            s = 2 * from + parity;
                            t = 2 * to + parity;
                            index = parity * 12 + from * 3 + to - (to > from ? 1 : 0);
                            differences[index]._next =
                                (unsigned(metric_in[s]()) - unsigned(metric_in[t]())) & 2097151;
                        }
                    }
                }
            }
        }
        if (reset) {
            for (unsigned i = 0; i < 8; ++i)
                metrics[i]._next = 0;
            for (unsigned i = 0; i < 24; ++i)
                differences[i]._next = 0;
        }
    }
    void _strobe() {
        for (unsigned i = 0; i < 8; ++i)
            metrics[i].strobe();
        for (unsigned i = 0; i < 24; ++i)
            differences[i].strobe();
    }
};
