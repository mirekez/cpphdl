#pragma once
#include "biased_driver.hpp"

namespace analog::one_g::ihp_sg13g2 {

class emphasized_mirror_bias : public resistor_mirror_bias {
public:
    void _assign() override {
        reference_resistance = resistance(1525);
        resistor_mirror_bias::_assign();
    }
};

// One shared physical bias feeds a main and a history steering bank. Their
// currents add on P/N, including finite headroom, loading and gate charge.
template <class Bias> class basic_emphasized_pam5_driver : public basic_biased_pam5_driver<Bias> {
    using Base = basic_biased_pam5_driver<Bias>;
public:
    std::array<port<electrical>, 4> HISTORY_P, HISTORY_N;
    std::array<electrical, 4> HISTORY_TAIL;
    // Preserve the nominal compact-model current after the physical reference
    // resistor correction. Actual PSP geometry is bound separately in the bench.
    static constexpr double nominal_tail_strength = .043990863665615935;
    mos_model unit_tail{channel_strength(nominal_tail_strength), voltage(.35)};
    double main_weight{.75}, history_weight{.25}, current_gain{2};
    capacitance_value tail_storage{110e-15}, gate_storage{180e-15}, gate_drain_storage{45e-15};
    capacitance_value unit_tail_gate_source{540e-15}, unit_tail_gate_drain{135e-15};
    capacitance_value bias_filter_storage{600e-12};

    void _assign() override {
        positive(main_weight, "main steering weight");
        positive(history_weight, "history steering weight");
        positive(current_gain, "steering current gain");
        this->tail_channel = unit_tail;
        this->tail_channel.beta = channel_strength(unit_tail.beta.value * current_gain * main_weight);
        Base::_assign();
        auto history_channel = unit_tail;
        history_channel.beta = channel_strength(unit_tail.beta.value * current_gain * history_weight);
        this->bias = capacitor(this->Vss, bias_filter_storage);
        for (unsigned cell = 0; cell < 4; ++cell) {
            HISTORY_TAIL[cell] = nmos(HISTORY_P[cell], this->P, this->steering_channel);
            HISTORY_TAIL[cell] = nmos(HISTORY_N[cell], this->N, this->steering_channel);
            this->Vss = nmos(this->bias, HISTORY_TAIL[cell], history_channel);
            HISTORY_TAIL[cell] = capacitor(this->Vss, tail_storage);
            this->TAIL[cell] = capacitor(this->Vss, tail_storage);
            HISTORY_P[cell] = capacitor(HISTORY_TAIL[cell], gate_storage);
            HISTORY_N[cell] = capacitor(HISTORY_TAIL[cell], gate_storage);
            this->GATE_P[cell] = capacitor(this->TAIL[cell], gate_storage);
            this->GATE_N[cell] = capacitor(this->TAIL[cell], gate_storage);
            HISTORY_P[cell] = capacitor(this->P, gate_drain_storage);
            HISTORY_N[cell] = capacitor(this->N, gate_drain_storage);
            this->GATE_P[cell] = capacitor(this->P, gate_drain_storage);
            this->GATE_N[cell] = capacitor(this->N, gate_drain_storage);
            this->bias = capacitor(this->Vss,
                capacitance_value(unit_tail_gate_source.value * current_gain * main_weight));
            this->bias = capacitor(this->TAIL[cell],
                capacitance_value(unit_tail_gate_drain.value * current_gain * main_weight));
            this->bias = capacitor(this->Vss,
                capacitance_value(unit_tail_gate_source.value * current_gain * history_weight));
            this->bias = capacitor(HISTORY_TAIL[cell],
                capacitance_value(unit_tail_gate_drain.value * current_gain * history_weight));
        }
    }
};
using emphasized_pam5_driver = basic_emphasized_pam5_driver<emphasized_mirror_bias>;
using trimmed_emphasized_pam5_driver = basic_emphasized_pam5_driver<trimmed_mirror_bias>;
} // namespace analog::one_g::ihp_sg13g2
