#pragma once
#include "../parameters.hpp"

namespace analog::one_g::ihp_sg13g2 {
// Four unary differential steering cells. Gate controls use the 3.3 V I/O domain.
// Each tail is an ideal supply-referenced bias current; its transistor bias circuit is not modeled.
class current_steering_driver : public IC {
public:
    std::array<port<electrical>, 4> GATE_P, GATE_N;
    std::array<electrical, 4> TAIL;
    _PORT(electrical) P;
    _PORT(electrical) N;
    resistance pullup{50};
    current unit_current{.005};
    capacitance_value storage{.5e-12};
    mos_model channel{channel_strength(.02), voltage(.7)};

    void _assign() override {
        positive(unit_current.value, "steering unit current");
        P = resistor(Vdd, pullup);
        N = resistor(Vdd, pullup);
        P = capacitor(Vdd, storage);
        N = capacitor(Vdd, storage);
        for (unsigned i = 0; i < TAIL.size(); ++i) {
            TAIL[i] = nmos(GATE_P[i], P, channel);
            TAIL[i] = nmos(GATE_N[i], N, channel);
            TAIL[i] = transconductor(Vss, Vss, Vdd, transconductance(unit_current.value / supply));
        }
    }
};
} // namespace analog::one_g::ihp_sg13g2
