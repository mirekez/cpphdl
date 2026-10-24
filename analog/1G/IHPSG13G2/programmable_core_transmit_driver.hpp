#pragma once
#include "core_transmit_driver.hpp"

namespace analog::one_g::ihp_sg13g2 {

// These active-low wires select actual parallel bias-resistor branches.
class programmable_emphasized_pam5_driver : public trimmed_emphasized_pam5_driver {
public:
    std::array<port<electrical>, 6> TRIM_N;

    void _assign() override {
        for (unsigned bit = 0; bit < 6; ++bit)
            reference.TRIM_N[bit] = TRIM_N[bit];
        trimmed_emphasized_pam5_driver::_assign();
    }
};

// Reuse the same main/history path. Six additional level translators expose
// the bias trim at the controller's core voltage, sharing the existing bias.
class programmable_core_emphasized_pam5_driver
    : public basic_core_emphasized_pam5_driver<programmable_emphasized_pam5_driver> {
    using Base = basic_core_emphasized_pam5_driver<programmable_emphasized_pam5_driver>;
public:
    std::array<port<electrical>, 6> TRIM_N;
    std::array<clock_level_shifter, 6> trim_gates;

    void _assign() override {
        for (unsigned bit = 0; bit < 6; ++bit)
            output.TRIM_N[bit] = trim_gates[bit].Y;
        Base::_assign();
        for (unsigned bit = 0; bit < 6; ++bit) {
            auto& gate = trim_gates[bit];
            gate.A = TRIM_N[bit];
            gate.Vlogic = Vlogic;
            gate.Vdd = Vdd;
            gate.Vss = Vss;
            gate.BIAS = translation_bias.BIAS;
            gate.elaborate();
        }
    }
};

} // namespace analog::one_g::ihp_sg13g2
