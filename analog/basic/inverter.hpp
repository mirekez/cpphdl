#pragma once
#include <analog.hpp>
#include "level_inverter.hpp"

namespace analog {

struct channel_model {
    mos_model transistor_parameters{};
    void operator()(const electrical& A, electrical& Y, const electrical& Vdd, electrical Vss) const {
        auto pmos_vdd = pmos::from(Vdd, transistor_parameters);
        Y = pmos_vdd(A); // First purpose: enable the output from Vdd when A is low.
        Vss = nmos(A, Y, transistor_parameters); // Second purpose: disable it by pulling toward Vss when A is high.
    }
};
struct fast_model {
    resistance drive_resistance{2000};
    analog::voltage low{.35};
    analog::voltage high{.85};

    void operator()(const electrical& A, electrical& Y, const electrical& Vdd, const electrical& Vss) const {
        level_inverter gate;
        gate.A = A;
        gate.Y = Y;
        gate.Vdd = Vdd;
        gate.Vss = Vss;
        gate.drive_resistance = drive_resistance;
        gate.low = low;
        gate.high = high;
        gate.elaborate();
    }
};
// A reusable IC type. Both model policies retain loading and shared-rail influence.
template <class Model = channel_model> class cmos : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;
    Model model{};

    void _assign() override {
        model(A, Y, Vdd, Vss);
    }
};

} // namespace analog
