#pragma once
#include <basic/cascode_voltage_buffer.hpp>
#include <basic/resistor_divider.hpp>

namespace analog::one_g::ihp_sg13g2 {

// A supply-ratio reference, not an ideal voltage or a precision bandgap. Its
// divider, feedback amplifier and finite output impedance interact with every
// connected receiver. The ADC uses the same supply as its conversion scale.
class receive_reference : public IC {
public:
    _PORT(electrical) OUT;
    resistor_divider reference, n_bias, p_bias;
    cascode_voltage_buffer buffer;
    electrical TAIL_BIAS;
    resistance reference_upper{1000}, reference_lower{1000};
    capacitance_value reference_storage{10e-12}, output_storage{30e-12};
    mos_model reference_channel{channel_strength(.03), voltage(.7)};

    void _assign() override {
        reference.TOP = Vdd;
        reference.BOTTOM = Vss;
        reference.upper = reference_upper;
        reference.lower = reference_lower;
        reference.elaborate();
        reference.TAP = capacitor(Vss, reference_storage);
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
        buffer.follower_channel = mos_model{channel_strength(.4), voltage(.7)};
        buffer.IN = reference.TAP;
        buffer.OUT = OUT;
        buffer.BIAS_N = n_bias.TAP;
        buffer.BIAS_P = p_bias.TAP;
        buffer.TAIL_BIAS = TAIL_BIAS;
        buffer.Vdd = Vdd;
        buffer.Vss = Vss;
        buffer.elaborate();
        OUT = capacitor(Vss, output_storage);
    }
};

// Center the verified 0.85..2.05 V ADC input window, instead of assuming
// that half of the 3.3 V supply is its best signal bias. The achieved nominal
// ratio is 1000/2275, approximately 1.45055 V at a 3.3 V supply.
class adc_range_reference : public receive_reference {
public:
    void _assign() override {
        reference_upper = resistance(1275);
        reference_lower = resistance(1000);
        receive_reference::_assign();
    }
};

} // namespace analog::one_g::ihp_sg13g2
