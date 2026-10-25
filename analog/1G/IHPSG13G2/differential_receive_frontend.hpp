#pragma once
#include "charge_balanced_receive_frontend.hpp"
#include "differential_receive_gain.hpp"
#include "../mdi_input_network.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Preserve both polarities of one differential input stage. Two current
// buffers drive the matched PGA strings; the reference is shared externally.
class differential_receive_preamplifier : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) VREF;
    _PORT(electrical) OUT;
    std::array<port<electrical>, 5> SELECT_N;
    nmos_resistor_amplifier amplifier;
    std::array<nmos_source_follower, 2> buffers;
    differential_receive_gain gain;
    resistance termination{100};
    capacitance_value output_storage{3e-12};

    void _assign() override {
        MINUS = resistor(PLUS, termination);
        amplifier.PLUS = PLUS;
        amplifier.MINUS = MINUS;
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.initial_input = voltage(0);
        amplifier.initial_supply = voltage(0);
        amplifier.load = resistance(500);
        amplifier.output_storage = capacitance_value(100e-15);
        amplifier.elaborate();
        for (unsigned path = 0; path < buffers.size(); ++path) {
            auto& buffer = buffers[path];
            buffer.IN = path == 0 ? amplifier.OUTP : amplifier.OUTN;
            buffer.OUT = path == 0 ? gain.IN_PLUS : gain.IN_MINUS;
            buffer.Vdd = Vdd;
            buffer.Vss = Vss;
            buffer.channel = mos_model{channel_strength(.1), voltage(.7)};
            buffer.bias_return = resistance(250);
            buffer.gate_source = capacitance_value(40e-15);
            buffer.gate_drain = capacitance_value(10e-15);
            buffer.output_storage = capacitance_value(40e-15);
            buffer.initial_input = voltage(0);
            buffer.initial_output = voltage(0);
            buffer.initial_supply = voltage(0);
            buffer.elaborate();
        }
        gain.OUT = OUT;
        gain.VREF = VREF;
        gain.Vdd = Vdd;
        gain.Vss = Vss;
        for (unsigned bit = 0; bit < SELECT_N.size(); ++bit)
            gain.SELECT_N[bit] = SELECT_N[bit];
        gain.initial_output = voltage(0);
        gain.initial_drive = voltage(0);
        gain.initial_supply = voltage(0);
        gain.elaborate();
        OUT = capacitor(Vss, output_storage);
    }
};

// Cold-start state is a circuit policy. The precharged characterization cell
// remains available; this cell starts all of its stored voltages at zero.
class cold_charge_balanced_receive_slice : public programmable_receive_slice {
public:
    capacitance_value sample_storage{1.5e-12};

    void _assign() override {
        // Independent dummy polarities balance opening charge across the
        // five launch phases: physical N20/ng2 and P42/ng5 at L=0.45 um.
        cancellation_n = mos_model{channel_strength(.016), voltage(.7)};
        cancellation_p = mos_model{channel_strength(.0084), voltage(.7)};
        // Lower switch resistance reduces tracking lag without removing hold
        // storage. Keep dummy charge cancellation independently adjustable.
        sampling_n = mos_model{channel_strength(.03), voltage(.7)};
        sampling_p = mos_model{channel_strength(.015), voltage(.7)};
        sampler.leakage_return = false;
        decision.weak_returns = false;
        sampler.storage = sample_storage;
        sampler.initial_input = voltage(0);
        sampler.initial_supply = voltage(0);
        preamplifier.initial_input = voltage(0);
        preamplifier.initial_supply = voltage(0);
        // Smaller input devices reduce DAC-to-held intrinsic gate coupling.
        // Double the resistors to retain the bias/gain with lower current;
        // the resulting settling time must pass the real decision schedule.
        preamplifier.channel = mos_model{channel_strength(.0075), voltage(.7)};
        preamplifier.load = resistance(1000);
        preamplifier.degeneration = resistance(100);
        preamplifier.tail_return = resistance(300);
        ladder.ladder = resistance(1000);
        programmable_receive_slice::_assign();
    }
};

template <std::size_t Count>
class cold_receive_adc_array : public SARADCCoreArray<Count, cold_charge_balanced_receive_slice> {
    using Base = SARADCCoreArray<Count, cold_charge_balanced_receive_slice>;
public:
    capacitance_value sample_storage{1.5e-12};

    void _assign() override {
        for (auto& slice : this->slices)
            slice.sample_storage = sample_storage;
        Base::_assign();
    }
};

// The analog preamplifier, sampling switches, DACs and decision latches stay
// coupled. Clock/gain controls are ports driven by their actual digital ICs.
template <std::size_t Count = 16>
class differential_receive_frontend : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) VREF;
    _PORT(electrical) Vlogic;
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CLEAR;
    std::array<port<electrical>, 5> SELECT_N;
    std::array<port<electrical>, Count> TRACK, CORE_DECISION;
    std::array<std::array<port<electrical>, 8>, Count> BIT_N;
    differential_receive_preamplifier preamplifier;
    cold_receive_adc_array<Count> adc;
    capacitance_value sample_storage{1.5e-12};

    void _assign() override {
        preamplifier.PLUS = PLUS;
        preamplifier.MINUS = MINUS;
        preamplifier.VREF = VREF;
        preamplifier.Vdd = Vdd;
        preamplifier.Vss = Vss;
        for (unsigned bit = 0; bit < SELECT_N.size(); ++bit)
            preamplifier.SELECT_N[bit] = SELECT_N[bit];
        preamplifier.elaborate();
        adc.sample_storage = sample_storage;
        adc.IN = preamplifier.OUT;
        adc.Vlogic = Vlogic;
        adc.EVALUATE = EVALUATE;
        adc.CLEAR = CLEAR;
        adc.Vdd = Vdd;
        adc.Vss = Vss;
        for (unsigned slice = 0; slice < Count; ++slice) {
            adc.TRACK[slice] = TRACK[slice];
            adc.CORE_DECISION[slice] = CORE_DECISION[slice];
            for (unsigned bit = 0; bit < 8; ++bit)
                adc.BIT_N[slice][bit] = BIT_N[slice][bit];
        }
        adc.elaborate();
    }
};

// No second termination is placed at P/N: the passive network and terminated
// receiver together form the actual MDI load seen by the two line drivers.
template <std::size_t Count = 16>
class differential_mdi_receiver : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) VREF;
    _PORT(electrical) Vlogic;
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CLEAR;
    std::array<port<electrical>, 5> SELECT_N;
    std::array<port<electrical>, Count> TRACK, CORE_DECISION;
    std::array<std::array<port<electrical>, 8>, Count> BIT_N;
    mdi_input_network input;
    differential_receive_frontend<Count> receiver;

    void _assign() override {
        input.P = P;
        input.N = N;
        input.Vdd = Vdd;
        input.Vss = Vss;
        input.elaborate();
        receiver.PLUS = input.PLUS;
        receiver.MINUS = input.MINUS;
        receiver.VREF = VREF;
        receiver.Vlogic = Vlogic;
        receiver.EVALUATE = EVALUATE;
        receiver.CLEAR = CLEAR;
        receiver.Vdd = Vdd;
        receiver.Vss = Vss;
        for (unsigned bit = 0; bit < SELECT_N.size(); ++bit)
            receiver.SELECT_N[bit] = SELECT_N[bit];
        for (unsigned slice = 0; slice < Count; ++slice) {
            receiver.TRACK[slice] = TRACK[slice];
            receiver.CORE_DECISION[slice] = CORE_DECISION[slice];
            for (unsigned bit = 0; bit < 8; ++bit)
                receiver.BIT_N[slice][bit] = BIT_N[slice][bit];
        }
        receiver.elaborate();
    }
};

} // namespace analog::one_g::ihp_sg13g2
