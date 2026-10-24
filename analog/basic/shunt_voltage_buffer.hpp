#pragma once
#include "cascode_ota.hpp"

namespace analog {

// Regulate a node that receives current from other circuits. A real pullup
// supplies the minimum load; feedback controls an NMOS sink. External bias
// ports let a parent share the bias generator across several functions.
class shunt_voltage_buffer : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    _PORT(electrical) BIAS_N;
    _PORT(electrical) BIAS_P;
    _PORT(electrical) TAIL_BIAS;
    electrical DRIVE;
    cascode_ota amplifier;
    mos_model sink_channel{channel_strength(.02), voltage(.7)};
    resistance pullup{1000};
    capacitance_value storage{20e-12}, compensation{.25e-12};

    void _assign() override {
        amplifier.PLUS = OUT;
        amplifier.MINUS = IN;
        amplifier.OUT = DRIVE;
        amplifier.BIAS_N = BIAS_N;
        amplifier.BIAS_P = BIAS_P;
        amplifier.TAIL_BIAS = TAIL_BIAS;
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.compensation = compensation;
        amplifier.initial_output = voltage(0);
        amplifier.elaborate();
        OUT = resistor(Vdd, pullup);
        Vss = nmos(DRIVE, OUT, sink_channel);
        OUT = capacitor(Vss, storage);
        DRIVE = capacitor(Vss, capacitance_value(80e-15));
        DRIVE = capacitor(OUT, capacitance_value(20e-15));
    }
};

} // namespace analog
