#pragma once
#include "receive_frontend.hpp"

namespace analog::one_g::ihp_sg13g2 {

// More stored charge reduces the voltage error caused by switch injection
// and off-state feedthrough. Acquisition must also charge twice the load.
template <std::size_t Count = 16> class precision_receive_frontend : public receive_frontend<Count> {
public:
    void _assign() override {
        for (auto& slice : this->adc.slices)
            slice.sampler.storage = capacitance_value(4e-12);
        receive_frontend<Count>::_assign();
    }
};
} // namespace analog::one_g::ihp_sg13g2
