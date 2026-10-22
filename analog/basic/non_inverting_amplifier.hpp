#pragma once
#include "opamp.hpp"
#include "resistor_divider.hpp"

namespace analog {

class non_inverting_amplifier : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    opamp amplifier;
    resistor_divider feedback;

    void _assign() override {
        feedback.TOP = OUT;
        feedback.BOTTOM = Vss;
        feedback.Vdd = Vdd;
        feedback.Vss = Vss;
        feedback.elaborate(); // Feed a loaded fraction of the output back to the input pair.

        amplifier.PLUS = IN;
        amplifier.MINUS = feedback.TAP;
        amplifier.OUT = OUT;
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.elaborate();
    }
};

} // namespace analog
