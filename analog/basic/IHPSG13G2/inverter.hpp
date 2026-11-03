#pragma once
#include "mos.hpp"

namespace analog::ihp {

// A physical CMOS topology. Select each transistor's PDK model and geometry
// before elaboration; neither device prescribes Y independently of its load.
class inverter : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;
    mos pull_up;
    mos pull_down;

    void _assign() override {
        // Enable the output from Vdd when A is low.
        pull_up.D = Y;
        pull_up.G = A;
        pull_up.S = Vdd;
        pull_up.B = Vdd;
        // Discharge the same output to Vss when A is high.
        pull_down.D = Y;
        pull_down.G = A;
        pull_down.S = Vss;
        pull_down.B = Vss;
        pull_up.elaborate();
        pull_down.elaborate();
    }
};

} // namespace analog::ihp
