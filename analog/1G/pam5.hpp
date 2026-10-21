#pragma once
#include "parameters.hpp"

namespace analog::one_g {
// Four thermometer controls select five levels. The voltage is relative to VCM.
class pam5_dac : public IC {
public:
    std::array<port<electrical>, 4> CODE;
    _PORT(electrical) VREF;
    _PORT(electrical) VCM;
    _PORT(electrical) OUT;
    double step = .5; // Open-circuit differential command, V per PAM level.
    resistance output_resistance{1000};
    capacitance_value storage{20e-15};

    void _assign() override {
        positive(step, "DAC level spacing");
        OUT = resistor(VCM, output_resistance);
        OUT = capacitor(VCM, storage);
        for (auto& bit : CODE)
            OUT = transconductor(VCM, bit, VREF, transconductance(step / (logic_supply * output_resistance.value)));
    }
};

// Explicit simulation approximation: four finite-drive, finite-charge comparators.
class sim_pam5_slicer : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) VCM;
    _PORT(electrical) VLOGIC;
    std::array<port<electrical>, 4> CODE;
    double step = symbol_step;
    voltage transition{.002};
    resistance drive{100};
    capacitance_value storage{20e-15};

    void _assign() override {
        positive(step, "slicer level spacing");
        positive(transition.value, "slicer transition width");
        if (2 * transition.value >= step)
            throw diagnostic("E_VALUE", "slicer transition regions must not overlap");
        for (unsigned i = 0; i < CODE.size(); ++i) {
            double threshold = (double(i) - 1.5) * step;
            CODE[i] = sim_controlled_resistor(VLOGIC, IN, VCM, drive, voltage(threshold - transition.value),
                                              voltage(threshold + transition.value), control_response::rising);
            Vss = sim_controlled_resistor(CODE[i], IN, VCM, drive, voltage(threshold - transition.value),
                                          voltage(threshold + transition.value), control_response::falling);
            CODE[i] = capacitor(Vss, storage, voltage(i < 2 ? logic_supply : 0));
        }
    }
};
} // namespace analog::one_g
