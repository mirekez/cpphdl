#pragma once
#include <analog.hpp>

namespace analog {

class pmos_current_mirror : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    mos_model model{channel_strength(.2), analog::voltage(.35)};
    resistance bias_return{1e9};

    void _assign() override {
        IN = pmos(IN, Vdd, model); // Diode connection establishes the common gate voltage.
        OUT = pmos(IN, Vdd, model); // Matched device supplies the other branch.
        Vdd = resistor(IN, bias_return); // Establish a defined gate voltage when the input branch is off.
    }
};

} // namespace analog
