#pragma once
#include <analog.hpp>

namespace circuits {
class Inverter : public analog::IC {
public:
    _PORT(analog::electrical) A;
    _PORT(analog::electrical) Y;
    analog::mos_model transistor_parameters{};

    void _assign() override {
        auto pmos_vdd = analog::pmos::from(Vdd, transistor_parameters);
        Y = pmos_vdd(A); // First purpose: enable Y from Vdd at low A.
        Vss = analog::nmos(A, Y, transistor_parameters); // Correct Y toward Vss at high A.
    }
};
} // namespace circuits
