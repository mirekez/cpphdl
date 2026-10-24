#pragma once
#include <analog.hpp>

namespace analog {

// Cascode transconductance stage. Its output is a current-steering node;
// connected feedback and loading set its voltage. Storage retains node charge.
// Bias and output loading remain explicit ports and connected components.
class cascode_ota : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) OUT;
    _PORT(electrical) BIAS_N;
    _PORT(electrical) BIAS_P;
    _PORT(electrical) TAIL_BIAS;
    electrical LEFT, RIGHT, MIRROR, PMIRROR, POUTPUT, TAIL;
    mos_model input_channel{channel_strength(.03), voltage(.7)};
    mos_model n_cascode_channel{channel_strength(.06), voltage(.7)};
    mos_model load_channel{channel_strength(.03), voltage(.7)};
    mos_model p_cascode_channel{channel_strength(.06), voltage(.7)};
    mos_model tail_channel{channel_strength(.06), voltage(.7)};
    capacitance_value compensation{1e-12};
    capacitance_value internal_storage{40e-15};
    capacitance_value input_gate_source{40e-15}, input_gate_drain{10e-15};
    voltage initial_output{0};

    void _assign() override {
        TAIL = nmos(PLUS, LEFT, input_channel);
        TAIL = nmos(MINUS, RIGHT, input_channel);
        LEFT = nmos(BIAS_N, MIRROR, n_cascode_channel);
        RIGHT = nmos(BIAS_N, OUT, n_cascode_channel);
        PMIRROR = pmos(MIRROR, Vdd, load_channel);
        POUTPUT = pmos(MIRROR, Vdd, load_channel);
        MIRROR = pmos(BIAS_P, PMIRROR, p_cascode_channel);
        OUT = pmos(BIAS_P, POUTPUT, p_cascode_channel);
        Vss = nmos(TAIL_BIAS, TAIL, tail_channel);
        OUT = capacitor(Vss, compensation, initial_output);
        LEFT = capacitor(Vss, internal_storage);
        RIGHT = capacitor(Vss, internal_storage);
        TAIL = capacitor(Vss, internal_storage);
        PMIRROR = capacitor(Vdd, internal_storage);
        POUTPUT = capacitor(Vdd, internal_storage);
        MIRROR = capacitor(Vss, internal_storage);
        PLUS = capacitor(TAIL, input_gate_source);
        MINUS = capacitor(TAIL, input_gate_source);
        PLUS = capacitor(LEFT, input_gate_drain);
        MINUS = capacitor(RIGHT, input_gate_drain);
    }
};

} // namespace analog
