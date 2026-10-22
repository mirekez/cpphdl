#pragma once
#include <analog.hpp>

namespace analog {

// Clock low precharges all four decision nodes. Clock high evaluates PLUS - MINUS.
// Both outputs are high during precharge; sample the complementary decision late
// in the high phase. No Boolean comparison or ideal voltage source drives a node.
class strongarm_comparator : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) CLOCK;
    _PORT(electrical) OUTP;
    _PORT(electrical) OUTN;
    electrical LEFT, RIGHT, TAIL;
    mos_model input_channel{channel_strength(.008), voltage(.7)};
    mos_model feedback_n{channel_strength(.004), voltage(.7)};
    mos_model feedback_p{channel_strength(.002), voltage(.7)};
    mos_model precharge{channel_strength(.002), voltage(.7)};
    mos_model tail_channel{channel_strength(.008), voltage(.7)};
    capacitance_value internal_storage{.03e-12}, output_storage{.05e-12};
    capacitance_value gate_source{.02e-12}, gate_drain{.003e-12};
    voltage initial_input{1.65};
    std::optional<voltage> initial_minus;

    void _assign() override {
        TAIL = nmos(PLUS, LEFT, input_channel);
        TAIL = nmos(MINUS, RIGHT, input_channel);
        Vss = nmos(CLOCK, TAIL, tail_channel);

        // PLUS discharges LEFT and OUTN first. Positive feedback raises OUTP.
        LEFT = nmos(OUTP, OUTN, feedback_n);
        RIGHT = nmos(OUTN, OUTP, feedback_n);
        OUTP = pmos(OUTN, Vdd, feedback_p);
        OUTN = pmos(OUTP, Vdd, feedback_p);

        LEFT = pmos(CLOCK, Vdd, precharge);
        RIGHT = pmos(CLOCK, Vdd, precharge);
        OUTP = pmos(CLOCK, Vdd, precharge);
        OUTN = pmos(CLOCK, Vdd, precharge);
        LEFT = capacitor(Vss, internal_storage);
        RIGHT = capacitor(Vss, internal_storage);
        TAIL = capacitor(Vss, internal_storage);
        OUTP = capacitor(Vss, output_storage);
        OUTN = capacitor(Vss, output_storage);
        PLUS = capacitor(TAIL, gate_source, initial_input);
        MINUS = capacitor(TAIL, gate_source, initial_minus.value_or(initial_input));
        PLUS = capacitor(LEFT, gate_drain, initial_input);
        MINUS = capacitor(RIGHT, gate_drain, initial_minus.value_or(initial_input));
    }
};
} // namespace analog
