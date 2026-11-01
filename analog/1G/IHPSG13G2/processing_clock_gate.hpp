#pragma once
#include <basic/cmos_storage.hpp>

namespace analog::one_g::ihp_sg13g2 {

// Acquisition runs the raw oscillator independently of the receiver. Retain
// its admission request while CLOCK is high so withdrawal cannot cut a pulse.
class processing_clock_gate : public IC {
public:
    _PORT(electrical) CLOCK;
    _PORT(electrical) ENABLE;
    _PORT(electrical) OUT;
    cmos<channel_model> clock_inverse, output_driver;
    cmos_d_latch admission;
    cmos_nand qualify_clock;

    void _assign() override {
        const mos_model logic{channel_strength(.004), voltage(.35)};
        clock_inverse.A = CLOCK;
        clock_inverse.Vdd = Vdd;
        clock_inverse.Vss = Vss;
        clock_inverse.model.transistor_parameters = logic;
        clock_inverse.elaborate();
        clock_inverse.Y = capacitor(Vss, capacitance_value(2e-15));

        admission.D = ENABLE;
        admission.ENABLE = clock_inverse.Y;
        admission.CLEAR = Vss;
        admission.Vdd = Vdd;
        admission.Vss = Vss;
        admission.channel = logic;
        admission.node_storage = capacitance_value(2e-15);
        admission.input_storage = capacitance_value(2e-15);
        admission.storage.weak_returns = false;
        for (auto* gate : {&admission.qualify_enable, &admission.set_gate, &admission.reset_gate})
            gate->weak_returns = false;
        admission.elaborate();

        qualify_clock.A = CLOCK;
        qualify_clock.B = admission.Q;
        qualify_clock.Y = output_driver.A;
        qualify_clock.Vdd = Vdd;
        qualify_clock.Vss = Vss;
        qualify_clock.parameters = logic;
        qualify_clock.weak_returns = false;
        qualify_clock.output_capacitance = capacitance_value(2e-15);
        qualify_clock.elaborate();
        output_driver.Y = OUT;
        output_driver.Vdd = Vdd;
        output_driver.Vss = Vss;
        output_driver.model.transistor_parameters = mos_model{channel_strength(.02), voltage(.35)};
        output_driver.elaborate();
        OUT = capacitor(Vss, capacitance_value(4e-15));
    }
};
} // namespace analog::one_g::ihp_sg13g2
