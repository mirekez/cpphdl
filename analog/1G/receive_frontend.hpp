#pragma once
#include "adc_core.hpp"
#include <basic/nmos_resistor_amplifier.hpp>
#include <basic/nmos_source_follower.hpp>

namespace analog::one_g {

// A shared amplifier and output buffer drive the interleaved ADC input.
// Sampling charge returns through the buffer and amplifier into their supplies.
template <std::size_t Count = 16, class Frontend = HighRateSARADCFeedbackFrontend>
class PAM5ReceiveFrontend : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) Vlogic;
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CLEAR;
    std::array<port<electrical>, Count> TRACK, CORE_DECISION;
    std::array<std::array<port<electrical>, 8>, Count> BIT_N;
    nmos_resistor_amplifier amplifier;
    nmos_source_follower buffer;
    SARADCCoreArray<Count, Frontend> adc;

    void _assign() override {
        amplifier.PLUS = PLUS;
        amplifier.MINUS = MINUS;
        // The lower drain resistance leaves headroom for the follower's Vgs.
        amplifier.load = resistance(500);
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.elaborate();
        buffer.IN = amplifier.OUTP;
        buffer.OUT = adc.IN;
        buffer.Vdd = Vdd;
        buffer.Vss = Vss;
        // Two overlapping acquisition windows load the common output. Scale
        // channel and bias together to preserve its DC transfer while reducing
        // output impedance; the doubled gate/output charge is retained too.
        buffer.channel = mos_model{channel_strength(.1), voltage(.7)};
        buffer.bias_return = resistance(250);
        buffer.gate_source = capacitance_value(40e-15);
        buffer.gate_drain = capacitance_value(10e-15);
        buffer.output_storage = capacitance_value(40e-15);
        // Match the amplifier's drain and the samplers' initial charges.
        buffer.initial_input = amplifier.initial_supply;
        buffer.initial_supply = amplifier.initial_supply;
        buffer.initial_output = voltage(1.65);
        buffer.elaborate();
        adc.EVALUATE = EVALUATE;
        adc.CLEAR = CLEAR;
        adc.Vlogic = Vlogic;
        adc.Vdd = Vdd;
        adc.Vss = Vss;
        for (std::size_t i = 0; i < Count; ++i) {
            adc.TRACK[i] = TRACK[i];
            adc.CORE_DECISION[i] = CORE_DECISION[i];
            for (std::size_t bit = 0; bit < 8; ++bit)
                adc.BIT_N[i][bit] = BIT_N[i][bit];
        }
        adc.elaborate();
    }
};
} // namespace analog::one_g
