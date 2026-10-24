#pragma once
#include "parallel_receive_frontend.hpp"
namespace analog::one_g::ihp_sg13g2 {

// Hold more sampled charge while the parallel input paths supply acquisition
// current. The additional capacitor still loads those same physical wires.
template <std::size_t Count = 16> class balanced_receive_frontend : public parallel_receive_frontend<Count> {
public:
    void _assign() override {
        for (auto& slice : this->adc.slices)
            slice.sampler.storage = capacitance_value(3e-12);
        parallel_receive_frontend<Count>::_assign();
    }
};
} // namespace analog::one_g::ihp_sg13g2
