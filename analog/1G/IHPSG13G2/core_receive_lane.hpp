#pragma once
#include "core_receive_slice.hpp"
#include "differential_receive_frontend.hpp"
#include "../biased_mdi_input_network.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Thirteen real slices produce the controller's thirteen interleaved samples.
// Slots 13..15 in the digital word are reserved; they have no analog devices.
class core_receive_lane : public IC {
public:
    static constexpr unsigned slice_count = 13;
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) Vlogic;
    _PORT(electrical) VCM;
    _PORT(electrical) VREF;
    _PORT(electrical) ADC_CLOCK;
    _PORT(electrical) EVALUATE_ENABLE;
    _PORT(electrical) CLEAR;
    std::array<port<electrical>, 5> GAIN_N;
    std::array<port<electrical>, slice_count> TRACK, CONVERT, DECISION;
    std::array<std::array<port<electrical>, 8>, slice_count> TRIAL;
    biased_mdi_input_network input;
    differential_receive_preamplifier preamplifier;
    level_shift_bias gain_bias;
    std::array<clock_level_shifter, 5> gain_controls;
    receive_evaluation_gate evaluation;
    std::array<core_receive_slice, slice_count> slices;

    void _assign() override {
        input.P = P;
        input.N = N;
        input.VCM = VCM;
        input.Vdd = Vdd;
        input.Vss = Vss;
        input.elaborate();
        gain_bias.Vdd = Vdd;
        gain_bias.Vss = Vss;
        gain_bias.Vlogic = Vlogic;
        gain_bias.elaborate();
        for (unsigned bit = 0; bit < 5; ++bit) {
            auto& gate = gain_controls[bit];
            gate.A = GAIN_N[bit];
            gate.Vdd = Vdd;
            gate.Vss = Vss;
            gate.Vlogic = Vlogic;
            gate.BIAS = gain_bias.BIAS;
            gate.elaborate();
            preamplifier.SELECT_N[bit] = gate.Y;
        }
        preamplifier.PLUS = input.PLUS;
        preamplifier.MINUS = input.MINUS;
        preamplifier.VREF = VREF;
        preamplifier.Vdd = Vdd;
        preamplifier.Vss = Vss;
        preamplifier.elaborate();
        evaluation.CLOCK = ADC_CLOCK;
        evaluation.ENABLE = EVALUATE_ENABLE;
        evaluation.Vdd = Vlogic;
        evaluation.Vss = Vss;
        evaluation.elaborate();
        for (unsigned index = 0; index < slice_count; ++index) {
            auto& slice = slices[index];
            slice.IN = preamplifier.OUT;
            slice.TRACK = TRACK[index];
            slice.EVALUATE = evaluation.OUT;
            slice.CONVERT = CONVERT[index];
            slice.CLEAR = CLEAR;
            slice.CORE_DECISION = DECISION[index];
            slice.Vdd = Vdd;
            slice.Vss = Vss;
            slice.Vlogic = Vlogic;
            for (unsigned bit = 0; bit < 8; ++bit)
                slice.TRIAL[bit] = TRIAL[index][bit];
            slice.elaborate();
        }
    }
};
} // namespace analog::one_g::ihp_sg13g2
