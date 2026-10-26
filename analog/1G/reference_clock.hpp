#pragma once
#include <basic/cmos_storage.hpp>
#include <basic/phase_frequency_detector.hpp>
#include <basic/pll.hpp>

namespace analog::one_g {

// Multiply a nominal 125 MHz reference by two. A physical flip-flop divides
// the oscillator output before a MOS phase/frequency detector and passive loop
// filter. This is the reference clock path; receive data timing is a separate
// loop and must not be mistaken for locking the slave to the local reference.
template <class Oscillator = ring_vco>
class basic_reference_clock_multiplier : public IC {
public:
    _PORT(electrical) REF;
    _PORT(electrical) CLEAR;
    _PORT(electrical) OUT;
    _PORT(electrical) DIVIDED;
    electrical FILTERED, CONTROL, PUMP, HIGH, LOW;
    cmos_divide2 feedback;
    cmos_phase_frequency_detector detector;
    lead_lag_filter filter;
    Oscillator oscillator;
    resistance smoothing_resistance{1000};
    capacitance_value smoothing_storage{3e-12};
    capacitance_value stage_storage{.05e-12}, logic_storage{5e-15};
    bool weak_pump_return{true};
    mos_model logic_channel{channel_strength(.01), voltage(.35)};

    void _assign() override {
        feedback.CLOCK = OUT;
        feedback.CLEAR = CLEAR;
        feedback.Q = DIVIDED;
        feedback.Vdd = Vdd;
        feedback.Vss = Vss;
        feedback.elaborate();
        detector.REF = REF;
        detector.FEEDBACK = DIVIDED;
        detector.CLEAR = CLEAR;
        detector.Vdd = Vdd;
        detector.Vss = Vss;
        detector.channel = logic_channel;
        detector.node_storage = logic_storage;
        detector.elaborate();
        // Finite switched resistive paths charge or discharge the filter.
        // Their currents depend on the achieved control and supply voltages.
        HIGH = pmos(detector.UP_N, Vdd, logic_channel);
        PUMP = resistor(HIGH, resistance(10000));
        LOW = nmos(detector.DOWN, PUMP, logic_channel);
        LOW = resistor(Vss, resistance(10000));
        if (weak_pump_return)
            HIGH = resistor(Vss, resistance(1e9));
        else
            HIGH = capacitor(Vss, logic_storage);
        PUMP = capacitor(Vss, logic_storage);
        filter.IN = PUMP;
        filter.OUT = FILTERED;
        filter.Vdd = Vdd;
        filter.Vss = Vss;
        filter.series = resistance(10000);
        filter.damping = resistance(2000);
        filter.storage = capacitance_value(20e-12);
        filter.initial = voltage(0); // Cold startup has no precharged tuning voltage.
        filter.elaborate();
        CONTROL = resistor(FILTERED, smoothing_resistance);
        CONTROL = capacitor(Vss, smoothing_storage);
        oscillator.CONTROL = CONTROL;
        oscillator.OUT = OUT;
        oscillator.Vdd = Vdd;
        oscillator.Vss = Vss;
        for (auto& stage : oscillator.stages)
            stage.stage_capacitance = stage_storage;
        oscillator.elaborate();
        for (auto& stage : oscillator.stages)
            CONTROL = capacitor(stage.TAIL, capacitance_value(1e-15));
        // Clear holds an oscillator stage while the divider and supply settle.
        Vss = nmos(CLEAR, oscillator.stages[1].Y, logic_channel);
        REF = capacitor(Vss, capacitance_value(2e-15));
    }
};
class reference_clock_multiplier : public basic_reference_clock_multiplier<> {};
} // namespace analog::one_g
