#pragma once
#include "parameters.hpp"

namespace analog::ten_g {
// Module-side DC blocks and a receiver termination. The receiving IC sets its own common mode.
class sfi_input : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) OUTP;
    _PORT(electrical) OUTN;
    _PORT(electrical) VCM;
    electrical BLOCKP, BLOCKN;
    capacitance_value blocking{100e-9};
    resistance termination{100};
    resistance esr{.1};

    void _assign() override {
        BLOCKP = resistor(P, esr);
        BLOCKN = resistor(N, esr);
        OUTP = capacitor(BLOCKP, blocking);
        OUTN = capacitor(BLOCKN, blocking);
        OUTP = resistor(VCM, resistance(termination.value / 2));
        OUTN = resistor(VCM, resistance(termination.value / 2));
    }
};
} // namespace analog::ten_g
