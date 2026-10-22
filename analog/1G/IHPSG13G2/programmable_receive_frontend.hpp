#pragma once
#include "receive_frontend.hpp"
#include "programmable_receive_gain.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Lower switch charge lets the complement driver open the sampler promptly.
// The parent declares storage; the connected acquisition tests check the
// increased channel resistance at the same 250 MS/s sampling rate.
class programmable_receive_slice : public SARADCFeedbackStage<buffered_nand_latch> {
public:
    void _assign() override {
        sampler.n_channel = mos_model{channel_strength(.02), voltage(.7)};
        sampler.p_channel = mos_model{channel_strength(.01), voltage(.7)};
        sampler.dummy_n = mos_model{channel_strength(.01), voltage(.7)};
        sampler.dummy_p = mos_model{channel_strength(.005), voltage(.7)};
        sampler.clock_storage = capacitance_value(20e-15);
        sampler.gate_coupling = capacitance_value(2e-15);
        sampler.off_coupling = capacitance_value(1e-15);
        SARADCFeedbackStage<buffered_nand_latch>::_assign();
    }
};

// Keep cable termination independent of gain. The high-impedance differential
// inputs precede current buffers, one programmable gain stage and the ADC.
template <std::size_t Count = 16> class programmable_receive_frontend : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) Vlogic;
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CLEAR;
    std::array<port<electrical>, 5> SELECT_N;
    std::array<port<electrical>, Count> TRACK, CORE_DECISION;
    std::array<std::array<port<electrical>, 8>, Count> BIT_N;
    std::array<nmos_resistor_amplifier, 2> amplifiers;
    std::array<nmos_source_follower, 2> buffers;
    programmable_receive_gain gain;
    SARADCCoreArray<Count, programmable_receive_slice> adc;
    resistance termination{100};
    capacitance_value gain_output_storage{3e-12};
    capacitance_value sample_storage{1.5e-12};

    void _assign() override {
        MINUS = resistor(PLUS, termination);
        for (std::size_t path = 0; path < amplifiers.size(); ++path) {
            auto& amplifier = amplifiers[path];
            auto& buffer = buffers[path];
            amplifier.PLUS = PLUS;
            amplifier.MINUS = MINUS;
            amplifier.OUTP = amplifiers[0].OUTP;
            amplifier.OUTN = amplifiers[0].OUTN;
            amplifier.load = resistance(500);
            amplifier.output_storage = capacitance_value(100e-15);
            amplifier.Vdd = Vdd;
            amplifier.Vss = Vss;
            amplifier.elaborate();
            buffer.IN = amplifier.OUTP;
            buffer.OUT = gain.IN;
            buffer.Vdd = Vdd;
            buffer.Vss = Vss;
            buffer.channel = mos_model(channel_strength(.1), voltage(.7));
            buffer.bias_return = resistance(250);
            buffer.gate_source = capacitance_value(40e-15);
            buffer.gate_drain = capacitance_value(10e-15);
            buffer.output_storage = capacitance_value(40e-15);
            buffer.initial_input = voltage(3.3);
            buffer.initial_output = voltage(1.65);
            buffer.initial_supply = voltage(3.3);
            buffer.elaborate();
        }
        gain.Vdd = Vdd;
        gain.Vss = Vss;
        for (std::size_t bit = 0; bit < SELECT_N.size(); ++bit)
            gain.SELECT_N[bit] = SELECT_N[bit];
        // Match the ADC's declared precharged input; warmup then solves the
        // achieved state through the resistors, transistors and stored charge.
        gain.initial_output = voltage(1.65);
        gain.initial_drive = voltage(2.4);
        gain.initial_supply = voltage(3.3);
        gain.OUT = capacitor(Vss, gain_output_storage, gain.initial_output);
        gain.elaborate();
        adc.IN = gain.OUT;
        adc.Vlogic = Vlogic;
        adc.EVALUATE = EVALUATE;
        adc.CLEAR = CLEAR;
        adc.Vdd = Vdd;
        adc.Vss = Vss;
        for (std::size_t slice = 0; slice < Count; ++slice) {
            adc.slices[slice].sampler.storage = sample_storage;
            adc.slices[slice].preamplifier.load = resistance(500);
            adc.slices[slice].ladder.ladder = resistance(1000);
            adc.TRACK[slice] = TRACK[slice];
            adc.CORE_DECISION[slice] = CORE_DECISION[slice];
            for (std::size_t bit = 0; bit < 8; ++bit)
                adc.BIT_N[slice][bit] = BIT_N[slice][bit];
        }
        adc.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
