#pragma once
#include "parameters.hpp"
#include "lock_monitor.hpp"
#include <basic/gm_stage.hpp>
#include <basic/sim_clocked_storage.hpp>
#include <basic/sim_xor.hpp>
#include <basic/ring_vco.hpp>

namespace analog::ten_g {
// Linear data phase detector: variable-width data/sample XOR minus half-cycle reference XOR.
class hogge_detector : public IC {
public:
    _PORT(electrical) DATA;
    _PORT(electrical) CLOCK;
    _PORT(electrical) ERROR;
    _PORT(electrical) REFERENCE;
    sim_dff sample, delayed;
    sim_xor error, reference;

    void _assign() override {
        sample.D = DATA;
        sample.CLK = CLOCK;
        delayed.D = sample.Q;
        delayed.CLK = CLOCK;
        delayed.falling_edge = true;
        for (auto* stage : {&sample, &delayed}) {
            stage->Vdd = Vdd;
            stage->Vss = Vss;
            stage->elaborate();
        }
        error.A = DATA;
        error.B = sample.Q;
        error.Y = ERROR;
        reference.A = sample.Q;
        reference.B = delayed.Q;
        reference.Y = REFERENCE;
        for (auto* gate : {&error, &reference}) {
            gate->Vdd = Vdd;
            gate->Vss = Vss;
            gate->elaborate();
        }
    }
};

// Run the ring at twice the line rate and divide by two: the Hogge reference needs equal half cycles.
class vco_10g : public IC {
public:
    _PORT(electrical) CONTROL;
    _PORT(electrical) OUT;
    ring_vco oscillator;
    sim_dff divider;
    gm_stage inverted;
    capacitance_value stage_storage{1.02e-15};

    void _assign() override {
        for (auto& stage : oscillator.stages)
            stage.stage_capacitance = stage_storage;
        oscillator.stages[2].initial = voltage(1.2);
        oscillator.CONTROL = CONTROL;
        oscillator.Vdd = Vdd;
        oscillator.Vss = Vss;
        oscillator.elaborate();
        inverted.PLUS = Vss;
        inverted.MINUS = divider.Q;
        inverted.BIAS = Vdd;
        inverted.gain = transconductance(.02);
        inverted.output_resistance = resistance(50);
        inverted.Vdd = Vdd;
        inverted.Vss = Vss;
        inverted.elaborate();
        divider.D = inverted.OUT;
        divider.CLK = oscillator.OUT;
        divider.Q = OUT;
        divider.Vdd = Vdd;
        divider.Vss = Vss;
        divider.elaborate();
    }
};

class pll_cdr : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) VCM;
    _PORT(electrical) CLOCK;
    _PORT(electrical) LOCK;
    lock_monitor qualification;
    electrical CONTROL, FILTER, FILTER_RETURN;
    limiting_amplifier input;
    hogge_detector detector;
    vco_10g oscillator;
    capacitance_value loop_storage{200e-15};
    resistance damping{5000};
    transconductance pump{.00001};
    voltage initial_control{.7};

    void _assign() override {
        input.PLUS = P;
        input.MINUS = N;
        input.BIAS = VCM;
        input.gain = transconductance(.1);
        input.clamp_resistance = resistance(.1);
        input.Vdd = Vdd;
        input.Vss = Vss;
        input.elaborate();
        detector.DATA = input.OUT;
        detector.CLOCK = CLOCK;
        detector.Vdd = Vdd;
        detector.Vss = Vss;
        detector.elaborate();
        FILTER = transconductor(Vss, detector.ERROR, detector.REFERENCE, pump);
        FILTER = capacitor(FILTER_RETURN, loop_storage, initial_control);
        Vss = resistor(FILTER_RETURN, damping);
        Vss = resistor(FILTER, resistance(1e9));
        CONTROL = resistor(FILTER, resistance(1000));
        CONTROL = capacitor(Vss, capacitance_value(100e-15), initial_control);
        CONTROL = diode(VCM, resistance(10), voltage(.35));
        VCM = diode(CONTROL, resistance(10), voltage(.1));
        oscillator.CONTROL = CONTROL;
        oscillator.OUT = CLOCK;
        oscillator.Vdd = Vdd;
        oscillator.Vss = Vss;
        oscillator.elaborate();
        qualification.ERROR = detector.ERROR;
        qualification.REFERENCE = detector.REFERENCE;
        qualification.VCM = VCM;
        qualification.LOCK = LOCK;
        qualification.Vdd = Vdd;
        qualification.Vss = Vss;
        qualification.elaborate();
    }
};
} // namespace analog::ten_g
