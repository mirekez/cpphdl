#pragma once
#include <basic/cascode_opamp.hpp>
#include <basic/nmos_source_follower.hpp>
#include <basic/resistor_divider.hpp>

namespace analog::one_g::ihp_sg13g2 {

// Inverting receive gain from a resistor string. One selected tap senses the
// string without carrying its signal current through the selector channel.
class programmable_receive_gain : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    std::array<port<electrical>, 5> SELECT_N;
    electrical FEEDBACK, DRIVE, BIAS, TAIL_BIAS;
    std::array<electrical, 8> TAP;
    std::array<electrical, 4> COMPENSATION;
    cascode_opamp amplifier;
    nmos_source_follower output_buffer;
    resistor_divider n_bias, p_bias;
    mos_model reference_channel{channel_strength(.03), voltage(.7)};
    mos_model selector_channel{channel_strength(.02), voltage(.7)};
    mos_model compensation_switch_channel{channel_strength(.02), voltage(.7)};
    resistance string_resistance{50}, mean_resistance{100000}, output_return{250};
    capacitance_value mean_storage{10e-12}, compensation_step{1e-12};
    voltage initial_output{0}, initial_drive{0}, initial_supply{0};

    void _assign() override {
        // Follow input common mode slowly, without putting signal current
        // through the reference. This also removes the previous bias follower.
        BIAS = resistor(IN, mean_resistance);
        BIAS = capacitor(Vss, mean_storage);
        n_bias.TOP = Vdd;
        n_bias.BOTTOM = Vss;
        n_bias.upper = resistance(15000);
        n_bias.lower = resistance(18000);
        n_bias.elaborate();
        p_bias.TOP = Vdd;
        p_bias.BOTTOM = Vss;
        p_bias.upper = resistance(23000);
        p_bias.lower = resistance(43000);
        p_bias.elaborate();
        n_bias.TAP = capacitor(Vss, capacitance_value(10e-12));
        p_bias.TAP = capacitor(Vss, capacitance_value(10e-12));
        TAIL_BIAS = resistor(Vdd, resistance(2500));
        Vss = nmos(TAIL_BIAS, TAIL_BIAS, reference_channel);
        TAIL_BIAS = capacitor(Vss, capacitance_value(10e-12));

        TAP[0] = resistor(OUT, string_resistance);
        for (unsigned i = 1; i < TAP.size(); ++i)
            TAP[i] = resistor(TAP[i - 1], string_resistance);
        IN = resistor(TAP[7], string_resistance);
        for (unsigned i = 0; i < SELECT_N.size(); ++i) {
            // Taps 4..8 give gains -0.8, -1.25, -2, -3.5 and -8.
            FEEDBACK = pmos(SELECT_N[i], TAP[i + 3], selector_channel);
            if (i < COMPENSATION.size()) {
                COMPENSATION[i] = pmos(SELECT_N[i], DRIVE, compensation_switch_channel);
                COMPENSATION[i] = capacitor(Vss, capacitance_value((4 - i) * compensation_step.value));
            }
        }
        amplifier.PLUS = BIAS;
        amplifier.MINUS = FEEDBACK;
        amplifier.OUT = DRIVE;
        amplifier.BIAS_N = n_bias.TAP;
        amplifier.BIAS_P = p_bias.TAP;
        amplifier.TAIL_BIAS = TAIL_BIAS;
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.compensation = compensation_step;
        amplifier.initial_output = initial_drive;
        amplifier.elaborate();
        output_buffer.IN = DRIVE;
        output_buffer.OUT = OUT;
        output_buffer.Vdd = Vdd;
        output_buffer.Vss = Vss;
        output_buffer.channel = mos_model(channel_strength(.2), voltage(.7));
        output_buffer.bias_return = output_return;
        output_buffer.gate_source = capacitance_value(80e-15);
        output_buffer.gate_drain = capacitance_value(20e-15);
        output_buffer.output_storage = capacitance_value(80e-15);
        output_buffer.initial_output = initial_output;
        output_buffer.initial_input = initial_drive;
        output_buffer.initial_supply = initial_supply;
        output_buffer.elaborate();
    }
};

} // namespace analog::one_g::ihp_sg13g2
