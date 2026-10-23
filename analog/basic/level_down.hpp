#pragma once
#include <analog.hpp>

namespace analog {

// A weak core PMOS supplies an inverted node; a high-voltage input NMOS can
// pull it down. A core inverter restores polarity. The ratioed input stage
// draws static current when A is high, and only the HV NMOS gate touches A.
class level_down : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;
    electrical inverted;
    mos_model input_n{channel_strength(.004), voltage(.7)};
    mos_model pull_up{channel_strength(.0005), voltage(.35)};
    mos_model output_n{channel_strength(.01), voltage(.35)};
    mos_model output_p{channel_strength(.005), voltage(.35)};
    capacitance_value node_storage{80e-15}, output_storage{10e-15};
    capacitance_value input_storage{5e-15}, feedback_storage{2e-15};

    void _assign() override {
        // Vdd is the core supply, typically 1.2 V, even when A reaches 3.3 V.
        inverted = pmos(Vss, Vdd, pull_up);
        Vss = nmos(A, inverted, input_n);
        Y = pmos(inverted, Vdd, output_p);
        Vss = nmos(inverted, Y, output_n);
        A = capacitor(Vss, input_storage);
        A = capacitor(Vdd, input_storage);
        A = capacitor(inverted, feedback_storage);
        inverted = capacitor(Vss, node_storage);
        inverted = capacitor(Vdd, input_storage);
        inverted = capacitor(Y, feedback_storage);
        Y = capacitor(Vss, output_storage);
    }
};
} // namespace analog
