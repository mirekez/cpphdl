#pragma once
#include "../reference_clock_domain.hpp"
#include <basic/balanced_ring_vco.hpp>

namespace analog::one_g::ihp_sg13g2 {

// The IHP implementation uses smaller low-voltage storage gates than the
// generic reference circuit. Their explicit process dimensions are checked by
// reference_clock_process.py. Local charge storage supports switching current
// through the finite supply impedance; no source drives the oscillator output.
class reference_clock_generator : public one_g::basic_reference_clock_domain<
    one_g::basic_reference_clock_multiplier<balanced_ring_vco>> {
    using clock_domain = one_g::basic_reference_clock_domain<one_g::basic_reference_clock_multiplier<balanced_ring_vco>>;
public:
    void _assign() override {
        multiplier.logic_channel = mos_model{channel_strength(.002), voltage(.35)};
        multiplier.feedback.storage.channel = multiplier.logic_channel;
        // Floating NAND stacks and the disconnected pump branch retain charge.
        // Each capacitor is part of the physical circuit and subsequent PEX.
        multiplier.weak_pump_return = false;
        for (auto* storage : {&multiplier.feedback.storage,
                              &multiplier.detector.reference,
                              &multiplier.detector.feedback}) {
            for (auto* latch : {&storage->master, &storage->slave}) {
                latch->storage.weak_returns = false;
                for (auto* gate : {&latch->qualify_enable, &latch->set_gate, &latch->reset_gate})
                    gate->weak_returns = false;
            }
        }
        multiplier.detector.both.weak_returns = false;
        multiplier.detector.reset.weak_returns = false;
        multiplier.stage_storage = capacitance_value(25e-15);
        logic_decoupling = capacitance_value(20e-12);
        clock_domain::_assign();
    }
};
} // namespace analog::one_g::ihp_sg13g2
