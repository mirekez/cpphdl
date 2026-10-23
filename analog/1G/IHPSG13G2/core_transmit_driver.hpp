#pragma once
#include "emphasized_driver.hpp"
#include "../clock_level_shifter.hpp"

namespace analog::one_g::ihp_sg13g2 {

// The sixteen independent inputs preserve the controller's all-gates-low
// disable state. A complementary pair alone cannot express that state.
class core_emphasized_pam5_driver : public IC {
public:
    _PORT(electrical) Vlogic;
    std::array<port<electrical>, 4> MAIN_P, MAIN_N, HISTORY_P, HISTORY_N;
    _PORT(electrical) P;
    _PORT(electrical) N;
    level_shift_bias translation_bias;
    std::array<clock_level_shifter, 16> gates;
    emphasized_pam5_driver output;
    capacitance_value shared_translation_storage{50e-12};

    void _assign() override {
        translation_bias.Vlogic = Vlogic;
        translation_bias.Vdd = Vdd;
        translation_bias.Vss = Vss;
        translation_bias.storage = shared_translation_storage;
        translation_bias.elaborate();
        for (unsigned bank = 0; bank < 4; ++bank)
            for (unsigned bit = 0; bit < 4; ++bit) {
                auto& gate = gates[bank * 4 + bit];
                gate.A = bank == 0 ? MAIN_P[bit] : bank == 1 ? MAIN_N[bit]
                       : bank == 2 ? HISTORY_P[bit] : HISTORY_N[bit];
                gate.Vlogic = Vlogic;
                gate.Vdd = Vdd;
                gate.Vss = Vss;
                gate.BIAS = translation_bias.BIAS;
                if (bank == 0)
                    output.GATE_P[bit] = gate.Y;
                else if (bank == 1)
                    output.GATE_N[bit] = gate.Y;
                else if (bank == 2)
                    output.HISTORY_P[bit] = gate.Y;
                else
                    output.HISTORY_N[bit] = gate.Y;
                gate.elaborate();
            }
        output.P = P;
        output.N = N;
        output.Vdd = Vdd;
        output.Vss = Vss;
        output.elaborate();
    }
};

} // namespace analog::one_g::ihp_sg13g2
