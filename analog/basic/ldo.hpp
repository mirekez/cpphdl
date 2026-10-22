#pragma once
#include "opamp.hpp"
#include "resistor_divider.hpp"

namespace analog {
// Low dropout regulator: the pass channel, divider and error amplifier share the physical network.
class ldo : public IC {
public:
    _PORT(electrical) REF;
    _PORT(electrical) OUT;
    electrical CONTROL, FEEDBACK;
    mos_model pass_parameters{channel_strength(.2), voltage(.35)};
    opamp error;
    resistor_divider divider;

    void _assign() override {
        OUT = pmos(CONTROL, Vdd, pass_parameters); // Deliver load current from the input supply.
        divider.TOP = OUT;
        divider.BOTTOM = Vss;
        divider.TAP = FEEDBACK;
        divider.elaborate(); // Divide the output; default equal resistors request OUT = 2 * REF.
        error.PLUS = FEEDBACK;
        error.MINUS = REF;
        error.OUT = CONTROL; // Excess output raises the PMOS gate, reducing delivered current.
        error.Vdd = Vdd;
        error.Vss = Vss;
        error.elaborate();
    }
};
} // namespace analog
