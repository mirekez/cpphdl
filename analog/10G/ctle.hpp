#pragma once
#include "parameters.hpp"
#include <basic/gm_stage.hpp>

namespace analog::ten_g {
class ctle : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) VCM;
    _PORT(electrical) OUTP;
    _PORT(electrical) OUTN;
    electrical LEADP, LEADN;
    resistance upper{200}, lower{50};
    capacitance_value lead{400e-15};
    gm_stage positive, negative;
    double gain = 4;

    void _assign() override {
        LEADP = resistor(P, upper);
        LEADN = resistor(N, upper);
        VCM = resistor(LEADP, lower);
        VCM = resistor(LEADN, lower);
        P = capacitor(LEADP, lead);
        N = capacitor(LEADN, lead); // Each bypass creates a zero while the divider sets finite high-frequency gain.
        positive.PLUS = LEADP;
        positive.MINUS = VCM;
        positive.OUT = OUTP;
        negative.PLUS = LEADN;
        negative.MINUS = VCM;
        negative.OUT = OUTN;
        for (auto* stage : {&positive, &negative}) {
            stage->BIAS = VCM;
            stage->Vdd = Vdd;
            stage->Vss = Vss;
            stage->output_resistance = resistance(50);
            stage->gain = transconductance(gain / 50);
            stage->storage = capacitance_value(5e-15);
            stage->elaborate();
        }
    }
};
} // namespace analog::ten_g
