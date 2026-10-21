#pragma once
#include "parameters.hpp"

namespace analog::ten_g {
// The tap inputs are already aligned: PRE is look-ahead, MAIN is the cursor, POST is the previous symbol.
class tx_emphasis : public IC {
public:
    _PORT(electrical) PRE;
    _PORT(electrical) MAIN;
    _PORT(electrical) POST;
    _PORT(electrical) VCM;
    _PORT(electrical) P;
    _PORT(electrical) N;
    std::array<double, 3> taps{-.1, .8, -.1};
    resistance drive{50};
    capacitance_value output_storage{2e-15};
    double unloaded_half_swing = .4;

    void _assign() override {
        positive(drive.value, "TX drive");
        positive(unloaded_half_swing, "TX swing");
        double total = 0;
        for (double tap : taps) {
            finite(tap, "TX tap");
            total += std::abs(tap);
        }
        if (total > 1.0000001 || taps[1] <= 0)
            throw diagnostic("E_VALUE", "TX taps need positive cursor and absolute sum <= 1");
        P = resistor(VCM, drive);
        N = resistor(VCM, drive);
        std::array<electrical, 3> inputs{PRE, MAIN, POST};
        for (std::size_t i = 0; i < taps.size(); ++i) {
            double gm = taps[i] * unloaded_half_swing / (common_mode * drive.value);
            P = transconductor(VCM, inputs[i], VCM, transconductance(gm));
            N = transconductor(VCM, inputs[i], VCM, transconductance(-gm));
        }
        P = capacitor(VCM, output_storage);
        N = capacitor(VCM, output_storage);
    }
};
} // namespace analog::ten_g
