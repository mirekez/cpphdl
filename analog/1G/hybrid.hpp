#pragma once
#include "parameters.hpp"

namespace analog::one_g {
// Subtract a local transmit replica. Finite balance leaves an echo for the DSP.
class hybrid : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) LOCAL;
    _PORT(electrical) VCM;
    _PORT(electrical) OUT;
    double balance = 65.0 / 115;
    capacitance_value storage{50e-15};

    void _assign() override {
        finite(balance, "hybrid balance");
        if (balance < 0 || balance > 1)
            throw diagnostic("E_VALUE", "hybrid balance must be in [0, 1]");
        OUT = resistor(VCM, resistance(1000));
        OUT = capacitor(VCM, storage);
        OUT = transconductor(VCM, P, N, transconductance(.001));
        OUT = transconductor(VCM, VCM, LOCAL, transconductance(balance * .001));
    }
};
} // namespace analog::one_g
