#pragma once
#include "parameters.hpp"

namespace analog::one_g {
// Passive lead zero followed by a finite-bandwidth transconductance stage.
class ctle : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) VCM;
    _PORT(electrical) OUT;
    electrical LEAD;
    resistance upper{1000}, lower{1000}, output{1000};
    capacitance_value lead{1e-12}, storage{50e-15};
    double gain = 2.3; // DC gain 1.15 compensates the nominal short-channel divider.

    void _assign() override {
        positive(gain, "equalizer gain");
        LEAD = resistor(IN, upper);
        LEAD = resistor(VCM, lower);
        IN = capacitor(LEAD, lead);
        OUT = resistor(VCM, output);
        OUT = capacitor(VCM, storage);
        OUT = transconductor(VCM, LEAD, VCM, transconductance(gain / output.value));
    }
};
} // namespace analog::one_g
