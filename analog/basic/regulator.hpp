#pragma once
#include "opamp.hpp"

namespace analog {

class regulator : public IC {
public:
    _PORT(electrical) REF;
    _PORT(electrical) OUT;
    electrical CONTROL;
    mos_model pass_model{channel_strength(.02), analog::voltage(.35)};
    opamp error;

    void _assign() override {
        OUT = pmos(CONTROL, Vdd, pass_model); // Supply output current through the pass transistor.
        error.PLUS = OUT;
        error.MINUS = REF;
        error.OUT = CONTROL;
        error.Vdd = Vdd;
        error.Vss = Vss;
        error.elaborate(); // Excess output raises the gate and reduces pass current.
    }
};

} // namespace analog
