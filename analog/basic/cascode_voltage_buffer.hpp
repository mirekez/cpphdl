#pragma once
#include "cascode_ota.hpp"
#include "nmos_source_follower.hpp"

namespace analog {

// Feedback makes a voltage buffer from a current-steering OTA and a source
// follower. Biases are ports so one physical bias network can serve several
// buffers. Output load and supply impedance influence the achieved voltage.
class cascode_voltage_buffer : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    _PORT(electrical) BIAS_N;
    _PORT(electrical) BIAS_P;
    _PORT(electrical) TAIL_BIAS;
    cascode_ota amplifier;
    nmos_source_follower follower;
    capacitance_value compensation{.25e-12};
    mos_model follower_channel{channel_strength(.2), voltage(.7)};

    void _assign() override {
        amplifier.PLUS = IN;
        amplifier.MINUS = OUT;
        amplifier.BIAS_N = BIAS_N;
        amplifier.BIAS_P = BIAS_P;
        amplifier.TAIL_BIAS = TAIL_BIAS;
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.compensation = compensation;
        amplifier.elaborate();
        follower.IN = amplifier.OUT;
        follower.OUT = OUT;
        follower.Vdd = Vdd;
        follower.Vss = Vss;
        follower.channel = follower_channel;
        follower.bias_return = resistance(250);
        follower.gate_source = capacitance_value(80e-15);
        follower.gate_drain = capacitance_value(20e-15);
        follower.output_storage = capacitance_value(80e-15);
        follower.elaborate();
    }
};

} // namespace analog
