#pragma once
#include "receive_frontend.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Retain the sampler's charge storage while shortening shared-amplifier
// settling. The process MOS model still contributes its intrinsic charge.
template <std::size_t Count = 16> class wideband_receive_frontend : public receive_frontend<Count> {
public:
    void _assign() override {
        this->amplifier.output_storage = capacitance_value(100e-15);
        // Keep the isolating drain charge, but restore the comparator input
        // before its evaluation edge. Lower resistance also absorbs kickback.
        for (auto& slice : this->adc.slices)
            slice.preamplifier.load = resistance(500);
        receive_frontend<Count>::_assign();
    }
};
} // namespace analog::one_g::ihp_sg13g2
