#pragma once
#include "programmable_receive_frontend.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Reduce compensation to raise closed-loop bandwidth. The shared circuit,
// selector loads and sampler still determine settling, stability and gain.
template <std::size_t Count = 16>
class wideband_programmable_receive_frontend : public programmable_receive_frontend<Count> {
public:
    void _assign() override {
        this->gain.compensation_step = capacitance_value(.25e-12);
        programmable_receive_frontend<Count>::_assign();
    }
};
} // namespace analog::one_g::ihp_sg13g2
