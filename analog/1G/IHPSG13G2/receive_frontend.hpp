#pragma once
#include "../receive_frontend.hpp"
#include <basic/buffered_nand_latch.hpp>

namespace analog::one_g::ihp_sg13g2 {

// PSP input-gate charge loads the SAR reference ladder as well as the sampler.
// Lower ladder resistance provides settling before opposite-phase evaluation.
using receive_slice = HighRateSARADCFeedbackStage<buffered_nand_latch>;
template <std::size_t Count = 16> class receive_frontend : public PAM5ReceiveFrontend<Count, receive_slice> {
public:
    void _assign() override {
        for (auto& slice : this->adc.slices)
            slice.ladder.ladder = resistance(1000);
        PAM5ReceiveFrontend<Count, receive_slice>::_assign();
    }
};
} // namespace analog::one_g::ihp_sg13g2
