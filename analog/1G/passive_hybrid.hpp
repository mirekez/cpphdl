#pragma once
#include "parameters.hpp"

namespace analog::one_g {
// A resistor bridge mixes line voltage with the opposite local transmit leg.
// Subtraction is passive: attenuation and reciprocal loading are unavoidable.
class passive_hybrid : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) LOCAL_P;
    _PORT(electrical) LOCAL_N;
    _PORT(electrical) VCM;
    _PORT(electrical) OUTP;
    _PORT(electrical) OUTN;
    resistance line_arm{2000};
    double balance = .35;
    capacitance_value storage{.05e-12};

    void _assign() override {
        positive(balance, "passive hybrid balance");
        if (balance > 1)
            throw diagnostic("E_VALUE", "passive hybrid balance must be in (0, 1]");
        OUTP = resistor(P, line_arm);
        OUTP = resistor(LOCAL_N, resistance(line_arm.value / balance));
        OUTN = resistor(N, line_arm);
        OUTN = resistor(LOCAL_P, resistance(line_arm.value / balance));
        OUTP = capacitor(VCM, storage);
        OUTN = capacitor(VCM, storage);
    }
};
} // namespace analog::one_g
