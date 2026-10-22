#pragma once
#include "opamp.hpp"
#include "resistor_divider.hpp"

namespace analog {
class variable_gain_amplifier : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    _PORT(electrical) BIAS;
    _PORT(electrical) CONTROL;
    electrical ATTENUATED;
    mos_model attenuation_parameters{channel_strength(.0004), voltage(.35)};
    resistor_divider feedback;
    opamp amplifier;

    void _assign() override {
        ATTENUATED = nmos(CONTROL, IN, attenuation_parameters);
        BIAS = resistor(ATTENUATED, resistance(10000)); // Channel resistance and this return set input attenuation.
        feedback.TOP = OUT;
        feedback.BOTTOM = BIAS;
        feedback.upper = resistance(30000);
        feedback.lower = resistance(10000);
        feedback.elaborate();
        amplifier.PLUS = ATTENUATED;
        amplifier.MINUS = feedback.TAP;
        amplifier.OUT = OUT;
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.elaborate(); // Closed-loop gain approaches four around BIAS when headroom permits.
    }
};

class mos_peak_detector : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    mos_model rectifier_parameters{channel_strength(.02), voltage(.35)};
    capacitance_value storage{10e-9};
    resistance decay{500000};
    voltage initial{0};

    void _assign() override {
        OUT = nmos(IN, IN, rectifier_parameters); // Diode-connected MOS charges toward positive peaks minus Vgs.
        OUT = capacitor(Vss, storage, initial);
        Vss = resistor(OUT, decay); // Release is physical discharge, not an ideal envelope operation.
    }
};

class error_integrator : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) REF;
    _PORT(electrical) OUT;
    electrical SUM;
    resistance input_resistance{1e6};
    capacitance_value integration{1e-9};
    opamp amplifier;
    voltage initial_difference{0};

    void _assign() override {
        SUM = resistor(IN, input_resistance);
        SUM = capacitor(OUT, integration, initial_difference); // Capacitive negative feedback integrates REF - IN.
        amplifier.PLUS = REF;
        amplifier.MINUS = SUM;
        amplifier.OUT = OUT;
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.elaborate();
    }
};

class agc : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    _PORT(electrical) BIAS;
    _PORT(electrical) REF;
    electrical CONTROL, ENVELOPE;
    variable_gain_amplifier gain;
    mos_peak_detector detector;
    error_integrator controller;

    void _assign() override {
        gain.IN = IN;
        gain.OUT = OUT;
        gain.BIAS = BIAS;
        gain.CONTROL = CONTROL;
        gain.Vdd = Vdd;
        gain.Vss = Vss;
        gain.elaborate();
        detector.IN = OUT;
        detector.OUT = ENVELOPE;
        detector.Vdd = Vdd;
        detector.Vss = Vss;
        detector.elaborate();
        controller.IN = ENVELOPE;
        controller.REF = REF;
        controller.OUT = CONTROL;
        controller.Vdd = Vdd;
        controller.Vss = Vss;
        controller.elaborate(); // A low envelope raises gain; excessive peaks reduce it.
    }
};
} // namespace analog
