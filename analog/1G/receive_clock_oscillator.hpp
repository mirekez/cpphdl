#pragma once
#include <basic/mos_r2r_dac.hpp>
#include <basic/ring_vco.hpp>

namespace analog::one_g {

// Coarse/fine electrical clock actuator. Both DACs load each other through the
// blend resistors. The oscillator's current, gate charge and external load
// influence the achieved control voltage and clock frequency.
class receive_clock_oscillator : public IC {
public:
    std::array<port<electrical>, 8> COARSE_N, FINE_N;
    _PORT(electrical) RESET;
    _PORT(electrical) OUT;
    electrical CONTROL;
    mos_r2r_dac coarse, fine;
    ring_vco oscillator;
    resistance coarse_arm{10000}, fine_arm{5090000};
    capacitance_value control_storage{.1e-12}, stage_storage{.05e-12};
    capacitance_value control_gate_storage{1e-15};
    mos_model reset_channel{channel_strength(.01), voltage(.35)};

    void _assign() override {
        for (unsigned bit = 0; bit < 8; ++bit) {
            coarse.BIT_N[bit] = COARSE_N[bit];
            fine.BIT_N[bit] = FINE_N[bit];
        }
        coarse.Vdd = Vdd;
        coarse.Vss = Vss;
        fine.Vdd = Vdd;
        fine.Vss = Vss;
        coarse.elaborate();
        fine.elaborate();
        CONTROL = resistor(coarse.OUT, coarse_arm);
        CONTROL = resistor(fine.OUT, fine_arm);
        CONTROL = capacitor(Vss, control_storage);
        oscillator.CONTROL = CONTROL;
        oscillator.OUT = OUT;
        oscillator.Vdd = Vdd;
        oscillator.Vss = Vss;
        for (auto& stage : oscillator.stages)
            stage.stage_capacitance = stage_storage;
        oscillator.elaborate();
        for (auto& stage : oscillator.stages)
            CONTROL = capacitor(stage.TAIL, control_gate_storage);
        // Hold one stage low during bias settling. Releasing it starts the
        // physical ring without relying on a simulator's noise or roundoff.
        Vss = nmos(RESET, oscillator.stages[1].Y, reset_channel);
    }
};
} // namespace analog::one_g
