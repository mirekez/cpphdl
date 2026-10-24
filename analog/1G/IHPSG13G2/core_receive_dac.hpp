#pragma once
#include "../clock_level_shifter.hpp"
#include <basic/mos_r2r_dac.hpp>

namespace analog::one_g::ihp_sg13g2 {

// Core trial bits reach the physical ladder through eight level translators.
// All translators load one replica bias; the ladder's gate charge also loads
// their outputs. Neither interface is an imposed high-voltage logic signal.
class core_receive_dac : public IC {
public:
    _PORT(electrical) Vlogic;
    std::array<port<electrical>, 8> TRIAL;
    _PORT(electrical) OUT;
    level_shift_bias translation_bias;
    std::array<clock_level_shifter, 8> controls;
    mos_r2r_dac ladder;
    capacitance_value bias_storage{25e-12};

    void _assign() override {
        translation_bias.Vlogic = Vlogic;
        translation_bias.Vdd = Vdd;
        translation_bias.Vss = Vss;
        translation_bias.storage = bias_storage;
        translation_bias.elaborate();
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto& control = controls[bit];
            control.A = TRIAL[bit];
            control.Vlogic = Vlogic;
            control.Vdd = Vdd;
            control.Vss = Vss;
            control.BIAS = translation_bias.BIAS;
            // The receiving ladder gates are a real capacitive load. Stronger
            // regeneration/output stages trade against smaller ladder drivers.
            control.pull_up = mos_model{channel_strength(.0028), voltage(.7)};
            control.output_channel = mos_model{channel_strength(.006), voltage(.7)};
            ladder.BIT_N[bit] = control.Y_N;
            control.elaborate();
        }
        ladder.Vdd = Vdd;
        ladder.Vss = Vss;
        ladder.OUT = OUT;
        ladder.rail_channel = mos_model{channel_strength(.04), voltage(.7)};
        ladder.ladder = resistance(750);
        ladder.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
