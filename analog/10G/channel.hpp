#pragma once
#include "parameters.hpp"

namespace analog::ten_g {
// Symmetric two-section lumped RC fixture with a real differential termination.
class channel : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) OUTP;
    _PORT(electrical) OUTN;
    _PORT(electrical) VCM;
    electrical MIDP, MIDN;
    resistance series{15}, termination{100};
    capacitance_value section{200e-15};

    void _assign() override {
        MIDP = resistor(P, series);
        MIDN = resistor(N, series);
        OUTP = resistor(MIDP, series);
        OUTN = resistor(MIDN, series);
        MIDP = capacitor(VCM, section);
        MIDN = capacitor(VCM, section);
        OUTP = capacitor(VCM, section);
        OUTN = capacitor(VCM, section);
        OUTN = resistor(OUTP, termination);
    }
};
} // namespace analog::ten_g
