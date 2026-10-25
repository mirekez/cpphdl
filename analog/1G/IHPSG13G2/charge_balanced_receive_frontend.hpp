#pragma once
#include "programmable_receive_frontend.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Increase the PMOS dummy's opposing charge while preserving the main switch,
// its clock driver, the hold capacitance and the complete decision circuit.
class charge_balanced_receive_slice : public programmable_receive_slice {
public:
    void _assign() override {
        cancellation_p = mos_model{channel_strength(.006), voltage(.7)};
        sampler.leakage_return = false;
        decision.weak_returns = false;
        programmable_receive_slice::_assign();
    }
};

template <std::size_t Count = 16>
class charge_balanced_receive_frontend
    : public programmable_receive_frontend<Count, charge_balanced_receive_slice> {
    using Base = programmable_receive_frontend<Count, charge_balanced_receive_slice>;
public:
    void _assign() override {
        this->gain.compensation_step = capacitance_value(.25e-12);
        Base::_assign();
    }
};

} // namespace analog::one_g::ihp_sg13g2
