#pragma once
#include <analog.hpp>

namespace analog {

// Differential gain from MOS current steering and physical load resistors.
// Source degeneration bounds gain; stored charge and external loading set speed.
class nmos_resistor_amplifier : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) OUTP;
    _PORT(electrical) OUTN;
    electrical LEFT_SOURCE, RIGHT_SOURCE, TAIL;
    resistance load{1000}, degeneration{50}, tail_return{150};
    capacitance_value output_storage{.3e-12}, tail_storage{.04e-12};
    capacitance_value gate_source{.15e-12}, gate_drain{.02e-12};
    mos_model channel{channel_strength(.015), voltage(.7)};
    voltage initial_input{1.65}, initial_supply{3.3};
    std::optional<voltage> initial_minus;

    void _assign() override {
        OUTN = resistor(Vdd, load); // PLUS steers current down the negative output.
        OUTP = resistor(Vdd, load);
        LEFT_SOURCE = nmos(PLUS, OUTN, channel);
        RIGHT_SOURCE = nmos(MINUS, OUTP, channel);
        LEFT_SOURCE = resistor(TAIL, degeneration);
        RIGHT_SOURCE = resistor(TAIL, degeneration);
        TAIL = resistor(Vss, tail_return); // Bias current follows the input/common-mode and supply physics.
        OUTN = capacitor(Vdd, output_storage);
        OUTP = capacitor(Vdd, output_storage);
        TAIL = capacitor(Vss, tail_storage);
        // Explicit input/feedback charge loads the preceding circuit too.
        PLUS = capacitor(LEFT_SOURCE, gate_source, initial_input);
        MINUS = capacitor(RIGHT_SOURCE, gate_source, initial_minus.value_or(initial_input));
        PLUS = capacitor(OUTN, gate_drain, voltage(initial_input.value - initial_supply.value));
        MINUS = capacitor(OUTP, gate_drain, voltage(initial_minus.value_or(initial_input).value - initial_supply.value));
    }
};
} // namespace analog
