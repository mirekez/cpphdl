#pragma once
#include <analog.hpp>

namespace analog {

// A fast inverter approximation made from two complementary controlled paths.
class level_inverter : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;
    resistance drive_resistance{2000};
    analog::voltage low{.35};
    analog::voltage high{.85};

    void _assign() override {
        Y = sim_controlled_resistor(Vdd, A, Vss, drive_resistance, low, high, control_response::falling);
        // Low A enables the path that supplies Y from Vdd.
        Vss = sim_controlled_resistor(Y, A, Vss, drive_resistance, low, high, control_response::rising);
        // High A enables the path that pulls the same Y toward Vss.
    }
};

} // namespace analog
