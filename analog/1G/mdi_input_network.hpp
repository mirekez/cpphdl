#pragma once
#include "parameters.hpp"

namespace analog::one_g {

// Attenuate the shared MDI signal and shift its common mode before the receiver.
// The receiver supplies the 100-ohm load at PLUS/MINUS. The transmitter's two
// 50-ohm pullups already terminate the MDI; do not add another 100 ohms there.
class mdi_input_network : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    electrical COMMON;
    resistance series{2000}, common_return{330}, reference_arm{1000};
    capacitance_value reference_storage{2e-12};

    void _assign() override {
        PLUS = resistor(P, series);
        MINUS = resistor(N, series);
        COMMON = resistor(Vdd, reference_arm);
        Vss = resistor(COMMON, reference_arm);
        PLUS = resistor(COMMON, common_return);
        MINUS = resistor(COMMON, common_return);
        COMMON = capacitor(Vss, reference_storage);
    }
};

} // namespace analog::one_g
