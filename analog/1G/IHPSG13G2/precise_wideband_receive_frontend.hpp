#pragma once
#include "wideband_receive_frontend.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Retain the faster amplifier and comparator bias while storing more sampled
// charge. The connected circuit must still meet the original acquisition time.
template <std::size_t Count = 16> class precise_wideband_receive_frontend : public wideband_receive_frontend<Count> {
public:
    void _assign() override {
        for (auto& slice : this->adc.slices)
            slice.sampler.storage = capacitance_value(4e-12);
        wideband_receive_frontend<Count>::_assign();
    }
};
} // namespace analog::one_g::ihp_sg13g2
