#pragma once
#include "../parameters.hpp"

namespace analog::one_g::ihp_sg13g2 {
// Resistor-fed diode-connected MOS establishes a bias; the output mirrors it.
// The generic model describes connectivity. PSP tests supply actual HV MOS sizes.
class resistor_mirror_bias : public IC {
public:
    _PORT(electrical) BIAS;
    resistance reference_resistance{1480};
    mos_model reference_channel{channel_strength(.014268727705112964), voltage(.35)};

    void _assign() override {
        BIAS = resistor(Vdd, reference_resistance);
        Vss = nmos(BIAS, BIAS, reference_channel);
    }
};

// Low gate voltage enables a binary-weighted parallel resistor branch. The
// switches participate electrically; a trim code never overrides bias voltage.
class trimmed_mirror_bias : public resistor_mirror_bias {
public:
    std::array<port<electrical>, 6> TRIM_N;
    std::array<electrical, 6> BRANCH;
    mos_model switch_channel{channel_strength(.02), voltage(.7)};

    void _assign() override {
        reference_resistance = resistance(1800);
        resistor_mirror_bias::_assign();
        for (unsigned bit = 0; bit < 6; ++bit) {
            BRANCH[bit] = pmos(TRIM_N[bit], Vdd, switch_channel);
            BIAS = resistor(BRANCH[bit], resistance(256000.0 / (1 << bit)));
        }
    }
};

// Four differential steering cells and physical MOS tail sinks. The external
// digital controls are gate voltages; the bias current is solved, not imposed.
template <class Bias> class basic_biased_pam5_driver : public IC {
public:
    std::array<port<electrical>, 4> GATE_P, GATE_N;
    std::array<electrical, 4> TAIL;
    _PORT(electrical) P;
    _PORT(electrical) N;
    electrical bias;
    Bias reference;
    resistance pullup{50};
    capacitance_value output_storage{.5e-12};
    mos_model steering_channel{channel_strength(.02), voltage(.7)};
    mos_model tail_channel{channel_strength(.04280618311533889), voltage(.35)};

    void _assign() override {
        reference.BIAS = bias;
        bind(reference, Vdd, Vss);
        P = resistor(Vdd, pullup);
        N = resistor(Vdd, pullup);
        P = capacitor(Vdd, output_storage);
        N = capacitor(Vdd, output_storage);
        for (unsigned cell = 0; cell < 4; ++cell) {
            TAIL[cell] = nmos(GATE_P[cell], P, steering_channel);
            TAIL[cell] = nmos(GATE_N[cell], N, steering_channel);
            Vss = nmos(bias, TAIL[cell], tail_channel);
        }
    }
};
using biased_pam5_driver = basic_biased_pam5_driver<resistor_mirror_bias>;
using trimmed_pam5_driver = basic_biased_pam5_driver<trimmed_mirror_bias>;
} // namespace analog::one_g::ihp_sg13g2
