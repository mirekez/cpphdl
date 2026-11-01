#pragma once
#include <basic/balanced_ring_vco.hpp>
#include <basic/mos_r2r_dac.hpp>

namespace analog::one_g::ihp_sg13g2 {

// The processing clock is independently trimmed against the external reference.
// Two loaded R/2R ladders bias a balanced ring; output buffers isolate the ring
// from the digital clock-tree input. Every port below is an electrical node.
class processing_clock_oscillator : public IC {
public:
    std::array<port<electrical>, 8> COARSE_N, FINE_N;
    _PORT(electrical) CLEAR;
    _PORT(electrical) OUT;
    electrical CONTROL, RING;
    mos_r2r_dac coarse, fine;
    balanced_ring_vco oscillator;
    cmos<channel_model> predriver, driver;
    capacitance_value stage_storage{50e-15};

    void _assign() override {
        for (unsigned bit = 0; bit < 8; ++bit) {
            coarse.BIT_N[bit] = COARSE_N[bit];
            fine.BIT_N[bit] = FINE_N[bit];
        }
        coarse.rail_channel = mos_model{channel_strength(.002), voltage(.35)};
        fine.rail_channel = coarse.rail_channel;
        coarse.Vdd = Vdd;
        coarse.Vss = Vss;
        fine.Vdd = Vdd;
        fine.Vss = Vss;
        coarse.elaborate();
        fine.elaborate();
        CONTROL = resistor(coarse.OUT, resistance(10000));
        CONTROL = resistor(fine.OUT, resistance(5090000));
        CONTROL = capacitor(Vss, capacitance_value(.1e-12));
        oscillator.CONTROL = CONTROL;
        oscillator.OUT = RING;
        oscillator.Vdd = Vdd;
        oscillator.Vss = Vss;
        for (auto& stage : oscillator.stages)
            stage.stage_capacitance = stage_storage;
        oscillator.elaborate();
        Vss = nmos(CLEAR, oscillator.stages[1].Y,
                   mos_model{channel_strength(.01), voltage(.35)});
        predriver.A = RING;
        predriver.Vdd = Vdd;
        predriver.Vss = Vss;
        predriver.model.transistor_parameters = mos_model{channel_strength(.004), voltage(.35)};
        predriver.elaborate();
        driver.A = predriver.Y;
        driver.Y = OUT;
        driver.Vdd = Vdd;
        driver.Vss = Vss;
        driver.model.transistor_parameters = mos_model{channel_strength(.02), voltage(.35)};
        driver.elaborate();
        RING = capacitor(Vss, capacitance_value(4e-15));
        predriver.Y = capacitor(Vss, capacitance_value(20e-15));
        OUT = capacitor(Vss, capacitance_value(4e-15));
        Vdd = capacitor(Vss, capacitance_value(5e-12));
    }
};
} // namespace analog::one_g::ihp_sg13g2
