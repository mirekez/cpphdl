#pragma once
#include <basic/resistor_divider.hpp>
#include <basic/shunt_voltage_buffer.hpp>

namespace analog::one_g::ihp_sg13g2 {

// Shared common-mode sink for the lower-attenuation input network. It must
// absorb real current from all attached cable legs; it is not an ideal VCM.
class receive_common_reference : public IC {
public:
    _PORT(electrical) OUT;
    electrical TAIL_BIAS;
    resistor_divider target, n_bias, p_bias;
    shunt_voltage_buffer buffer;
    mos_model tail_channel{channel_strength(.03), voltage(.7)};

    void _assign() override {
        target.TOP = Vdd;
        target.BOTTOM = Vss;
        target.upper = resistance(10000);
        target.lower = resistance(12000);
        target.elaborate();
        target.TAP = capacitor(Vss, capacitance_value(10e-12));
        n_bias.TOP = Vdd;
        n_bias.BOTTOM = Vss;
        n_bias.upper = resistance(12000);
        n_bias.lower = resistance(21000);
        n_bias.elaborate();
        n_bias.TAP = capacitor(Vss, capacitance_value(10e-12));
        p_bias.TOP = Vdd;
        p_bias.BOTTOM = Vss;
        p_bias.upper = resistance(23000);
        p_bias.lower = resistance(43000);
        p_bias.elaborate();
        p_bias.TAP = capacitor(Vss, capacitance_value(10e-12));
        TAIL_BIAS = resistor(Vdd, resistance(2500));
        Vss = nmos(TAIL_BIAS, TAIL_BIAS, tail_channel);
        TAIL_BIAS = capacitor(Vss, capacitance_value(10e-12));
        buffer.IN = target.TAP;
        buffer.OUT = OUT;
        buffer.BIAS_N = n_bias.TAP;
        buffer.BIAS_P = p_bias.TAP;
        buffer.TAIL_BIAS = TAIL_BIAS;
        buffer.Vdd = Vdd;
        buffer.Vss = Vss;
        buffer.elaborate();
    }
};

} // namespace analog::one_g::ihp_sg13g2
