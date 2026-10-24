#pragma once
#include "core_receive_dac.hpp"
#include <basic/nmos_resistor_amplifier.hpp>
#include <basic/strongarm_comparator.hpp>
#include <basic/buffered_nand_latch.hpp>
#include <basic/level_down.hpp>

namespace analog::one_g::ihp_sg13g2 {

// This DAC variant exposes its existing replica node for additional loads.
class shared_bias_receive_dac : public core_receive_dac {
public:
    _PORT(electrical) BIAS;

    void _assign() override {
        translation_bias.BIAS = BIAS;
        core_receive_dac::_assign();
    }
};

// A core trial word and evaluation edge produce a retained core-voltage
// decision. The sampler connects to IN; all translation and regeneration
// draw current from the same finite rails and replica bias as the DAC.
class core_receive_decision : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) Vlogic;
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CLEAR;
    _PORT(electrical) CORE_DECISION;
    std::array<port<electrical>, 8> TRIAL;
    shared_bias_receive_dac dac;
    std::array<clock_level_shifter, 2> timing;
    nmos_resistor_amplifier preamplifier;
    strongarm_comparator comparator;
    buffered_nand_latch decision;
    level_down core_interface;

    void _assign() override {
        dac.Vdd = Vdd;
        dac.Vss = Vss;
        dac.Vlogic = Vlogic;
        for (unsigned bit = 0; bit < TRIAL.size(); ++bit)
            dac.TRIAL[bit] = TRIAL[bit];
        dac.elaborate();
        for (unsigned i = 0; i < timing.size(); ++i) {
            auto& control = timing[i];
            control.A = i == 0 ? EVALUATE : CLEAR;
            control.Vlogic = Vlogic;
            control.Vdd = Vdd;
            control.Vss = Vss;
            control.BIAS = dac.BIAS;
            control.pull_up = mos_model{channel_strength(.0028), voltage(.7)};
            control.output_channel = mos_model{channel_strength(.006), voltage(.7)};
            control.elaborate();
        }
        timing[0].Y = capacitor(Vss, capacitance_value(100e-15));
        timing[1].Y = capacitor(Vss, capacitance_value(20e-15));
        preamplifier.PLUS = IN;
        preamplifier.MINUS = dac.OUT;
        preamplifier.Vdd = Vdd;
        preamplifier.Vss = Vss;
        preamplifier.channel = mos_model{channel_strength(.0075), voltage(.7)};
        preamplifier.load = resistance(1000);
        preamplifier.degeneration = resistance(100);
        preamplifier.tail_return = resistance(300);
        preamplifier.gate_source = capacitance_value(20e-15);
        preamplifier.gate_drain = capacitance_value(3e-15);
        preamplifier.initial_input = voltage(0);
        preamplifier.initial_supply = voltage(0);
        preamplifier.elaborate();
        comparator.PLUS = preamplifier.OUTP;
        comparator.MINUS = preamplifier.OUTN;
        comparator.CLOCK = timing[0].Y;
        comparator.Vdd = Vdd;
        comparator.Vss = Vss;
        comparator.initial_input = voltage(0);
        comparator.elaborate();
        decision.SET_N = comparator.OUTN;
        decision.RESET_N = comparator.OUTP;
        decision.CLEAR = timing[1].Y;
        decision.Vdd = Vdd;
        decision.Vss = Vss;
        decision.weak_returns = false;
        decision.elaborate();
        core_interface.A = decision.Q;
        core_interface.Y = CORE_DECISION;
        core_interface.Vdd = Vlogic;
        core_interface.Vss = Vss;
        core_interface.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
