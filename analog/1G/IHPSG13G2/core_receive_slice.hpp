#pragma once
#include "core_receive_decision.hpp"
#include <basic/mos_track_hold.hpp>
#include <basic/cmos_logic.hpp>
#include <basic/inverter.hpp>

namespace analog::one_g::ihp_sg13g2 {

// The controller changes its trial on the rising ADC edge. Evaluate during
// the opposite half-cycle, after the DAC has had time to settle. ENABLE must
// come from the qualified startup controller, not from an ideal clock source.
class receive_evaluation_gate : public IC {
public:
    _PORT(electrical) CLOCK;
    _PORT(electrical) ENABLE;
    _PORT(electrical) OUT;
    electrical disabled, stack;
    // Drive thirteen translated evaluation inputs. The smaller .004 gate
    // exceeded 1 ns at the slow IHP corner under the declared 728 fF load.
    mos_model channel{channel_strength(.016), voltage(.35)};

    void _assign() override {
        disabled = pmos(ENABLE, Vdd, channel);
        Vss = nmos(ENABLE, disabled, channel);
        disabled = capacitor(Vss, capacitance_value(5e-15));
        stack = pmos(CLOCK, Vdd, channel);
        OUT = pmos(disabled, stack, channel);
        Vss = nmos(CLOCK, OUT, channel);
        Vss = nmos(disabled, OUT, channel);
        stack = capacitor(Vss, capacitance_value(5e-15));
        OUT = capacitor(Vss, capacitance_value(10e-15));
    }
};

// Only the selected SAR slice sees the lane-wide evaluation pulse. This
// physical NAND/inverter pair stays in the coupled network with its loads.
class receive_conversion_gate : public IC {
public:
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CONVERT;
    _PORT(electrical) OUT;
    cmos_nand select;
    cmos<channel_model> selected;

    void _assign() override {
        select.A = EVALUATE;
        select.B = CONVERT;
        select.Vdd = Vdd;
        select.Vss = Vss;
        select.parameters = mos_model{channel_strength(.002), voltage(.35)};
        select.output_capacitance = capacitance_value(10e-15);
        select.weak_returns = false;
        select.elaborate();
        selected.A = select.Y;
        selected.Vdd = Vdd;
        selected.Vss = Vss;
        selected.Y = OUT;
        selected.model.transistor_parameters = mos_model{channel_strength(.002), voltage(.35)};
        selected.elaborate();
        OUT = capacitor(Vss, capacitance_value(10e-15));
    }
};

// Direct tracking control preserves the existing SAR startup/acquire schedule.
// This is a composed circuit candidate: the loaded translator, held-node
// kickback and first decision still require process and extracted verification.
class core_receive_slice : public IC {
public:
    _PORT(electrical) Vlogic;
    _PORT(electrical) IN;
    _PORT(electrical) TRACK;
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CONVERT;
    _PORT(electrical) CLEAR;
    _PORT(electrical) CORE_DECISION;
    std::array<port<electrical>, 8> TRIAL;
    clock_level_shifter tracking;
    receive_conversion_gate evaluation_gate;
    mos_track_hold sampler;
    core_receive_decision decision;

    void _assign() override {
        evaluation_gate.EVALUATE = EVALUATE;
        evaluation_gate.CONVERT = CONVERT;
        evaluation_gate.Vdd = Vlogic;
        evaluation_gate.Vss = Vss;
        evaluation_gate.elaborate();

        decision.IN = sampler.OUT;
        decision.Vdd = Vdd;
        decision.Vss = Vss;
        decision.Vlogic = Vlogic;
        decision.EVALUATE = evaluation_gate.OUT;
        decision.CLEAR = CLEAR;
        decision.CORE_DECISION = CORE_DECISION;
        for (unsigned bit = 0; bit < 8; ++bit)
            decision.TRIAL[bit] = TRIAL[bit];
        decision.elaborate();

        tracking.A = TRACK;
        tracking.Vdd = Vdd;
        tracking.Vss = Vss;
        tracking.Vlogic = Vlogic;
        // One additional load on the same finite replica used by the DAC.
        tracking.BIAS = decision.BIAS;
        tracking.pull_up = mos_model{channel_strength(.0028), voltage(.7)};
        tracking.output_channel = mos_model{channel_strength(.006), voltage(.7)};
        tracking.elaborate();
        sampler.IN = IN;
        sampler.CLOCK = tracking.Y;
        sampler.Vdd = Vdd;
        sampler.Vss = Vss;
        sampler.n_channel = mos_model{channel_strength(.03), voltage(.7)};
        sampler.p_channel = mos_model{channel_strength(.015), voltage(.7)};
        sampler.dummy_n = mos_model{channel_strength(.016), voltage(.7)};
        sampler.dummy_p = mos_model{channel_strength(.0084), voltage(.7)};
        sampler.storage = capacitance_value(1.5e-12);
        sampler.clock_storage = capacitance_value(20e-15);
        sampler.leakage_return = false;
        sampler.initial_input = voltage(0);
        sampler.initial_supply = voltage(0);
        sampler.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
