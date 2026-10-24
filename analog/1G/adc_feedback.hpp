#pragma once
#include "adc.hpp"
#include <basic/nand_latch.hpp>

namespace analog::one_g {

// Hold the resolved comparator bit through its precharge interval. The added
// cross-coupled MOS NAND gates draw supply current and load the comparator.
template <class Decision = cleared_nand_latch> class SARADCFeedbackStage : public SARADCFrontend {
public:
    _PORT(electrical) DECISION;
    _PORT(electrical) DECISION_N;
    _PORT(electrical) CLEAR;
    Decision decision;

    void _assign() override {
        SARADCFrontend::_assign();
        decision.SET_N = NEGATIVE;
        decision.RESET_N = POSITIVE;
        decision.CLEAR = CLEAR;
        decision.Q = DECISION;
        decision.Q_N = DECISION_N;
        decision.Vdd = Vdd;
        decision.Vss = Vss;
        decision.elaborate();
    }
};
using SARADCFeedbackFrontend = SARADCFeedbackStage<>;

// Wider sampling devices reduce acquisition resistance for the shared-input
// 250 MS/s bank. Their gate, clock and off-state coupling grow with width too;
// the extra loading is part of the circuit, not a solver setting.
template <class Decision = cleared_nand_latch> class HighRateSARADCFeedbackStage : public SARADCFeedbackStage<Decision> {
public:
    void _assign() override {
        this->sampler.n_channel = mos_model{channel_strength(.08), voltage(.7)};
        this->sampler.p_channel = mos_model{channel_strength(.04), voltage(.7)};
        this->sampler.dummy_n = mos_model{channel_strength(.04), voltage(.7)};
        this->sampler.dummy_p = mos_model{channel_strength(.02), voltage(.7)};
        this->sampler.clock_storage = capacitance_value(20e-15);
        this->sampler.gate_coupling = capacitance_value(8e-15);
        this->sampler.off_coupling = capacitance_value(4e-15);
        SARADCFeedbackStage<Decision>::_assign();
    }
};
using HighRateSARADCFeedbackFrontend = HighRateSARADCFeedbackStage<>;

template <std::size_t Count = 16, class Frontend = HighRateSARADCFeedbackFrontend> class SARADCArray : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CLEAR;
    std::array<port<electrical>, Count> TRACK, DECISION, DECISION_N;
    std::array<std::array<port<electrical>, 8>, Count> BIT_N;
    std::array<Frontend, Count> slices;

    void _assign() override {
        for (std::size_t i = 0; i < Count; ++i) {
            slices[i].IN = IN;
            slices[i].EVALUATE = EVALUATE;
            slices[i].CLEAR = CLEAR;
            slices[i].TRACK = TRACK[i];
            slices[i].DECISION = DECISION[i];
            slices[i].DECISION_N = DECISION_N[i];
            for (std::size_t bit = 0; bit < 8; ++bit)
                slices[i].BIT_N[bit] = BIT_N[i][bit];
            slices[i].Vdd = Vdd;
            slices[i].Vss = Vss;
            slices[i].elaborate();
        }
    }
};
} // namespace analog::one_g
