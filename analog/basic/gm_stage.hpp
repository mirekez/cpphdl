#pragma once
#include <analog.hpp>

namespace analog {
// An idealized gm cell with explicit output impedance and charge; not a hidden op-amp.
class gm_stage : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) BIAS;
    _PORT(electrical) OUT;
    transconductance gain{.01};
    resistance output_resistance{1000};
    capacitance_value storage{1e-15};
    voltage initial{0}; // Relative to BIAS.

    void _assign() override {
        OUT = resistor(BIAS, output_resistance);
        OUT = transconductor(BIAS, PLUS, MINUS, gain);
        OUT = capacitor(BIAS, storage, initial);
    }
};

class limiting_amplifier : public gm_stage {
public:
    resistance clamp_resistance{1};

    void _assign() override {
        gm_stage::_assign();
        OUT = diode(Vdd, clamp_resistance); // External loading can still change the achieved rail voltage.
        Vss = diode(OUT, clamp_resistance);
    }
};
} // namespace analog
