#pragma once
#include "core_transmit_driver.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Preserve the main/history current when a remote transmitter changes P/N.
// Each cascode isolates its current-setting drain from the steering node.
// The lower cascode bias leaves voltage headroom for both ends driving together.
class cascoded_emphasized_pam5_driver : public IC {
public:
    std::array<port<electrical>, 4> GATE_P, GATE_N, HISTORY_P, HISTORY_N;
    std::array<port<electrical>, 6> TRIM_N;
    _PORT(electrical) P;
    _PORT(electrical) N;
    electrical bias, CASCODE_BIAS;
    std::array<std::array<electrical, 4>, 2> TAIL, LOWER;
    trimmed_mirror_bias reference;
    resistance pullup{50}, cascode_upper{3000}, cascode_lower{3600};
    capacitance_value output_storage{.5e-12}, bias_filter_storage{400e-12};
    capacitance_value cascode_storage{50e-12}, tail_storage{110e-15};
    capacitance_value lower_storage_unit{30e-15};
    capacitance_value steering_gate_unit{180e-15}, steering_miller_unit{45e-15};
    capacitance_value sink_gate_unit{540e-15}, sink_miller_unit{135e-15};
    capacitance_value cascode_gate_unit{.9e-12}, cascode_miller_unit{.225e-12};
    mos_model unit_tail{channel_strength(emphasized_pam5_driver::nominal_tail_strength), voltage(.35)};

    void _assign() override {
        reference.BIAS = bias;
        for (unsigned bit = 0; bit < TRIM_N.size(); ++bit)
            reference.TRIM_N[bit] = TRIM_N[bit];
        bind(reference, Vdd, Vss);
        bias = capacitor(Vss, bias_filter_storage);
        CASCODE_BIAS = resistor(Vdd, cascode_upper);
        Vss = resistor(CASCODE_BIAS, cascode_lower);
        CASCODE_BIAS = capacitor(Vss, cascode_storage);
        P = resistor(Vdd, pullup);
        N = resistor(Vdd, pullup);
        P = capacitor(Vdd, output_storage);
        N = capacitor(Vdd, output_storage);
        for (unsigned bank = 0; bank < 2; ++bank) {
            const double weight = bank == 0 ? 1.5 : .5;
            const double steering_scale = bank == 0 ? 4 : 2;
            const double cascode_scale = bank == 0 ? 3 : 1;
            const mos_model steering{channel_strength(.02 * steering_scale), voltage(.7)};
            const mos_model cascode{channel_strength(.1 * cascode_scale), voltage(.7)};
            auto sink = unit_tail;
            sink.beta = channel_strength(unit_tail.beta.value * weight);
            for (unsigned cell = 0; cell < 4; ++cell) {
                auto& positive = bank == 0 ? GATE_P[cell] : HISTORY_P[cell];
                auto& negative = bank == 0 ? GATE_N[cell] : HISTORY_N[cell];
                auto& tail = TAIL[bank][cell];
                auto& lower = LOWER[bank][cell];
                tail = nmos(positive, P, steering);
                tail = nmos(negative, N, steering);
                lower = nmos(CASCODE_BIAS, tail, cascode);
                Vss = nmos(bias, lower, sink);
                tail = capacitor(Vss, tail_storage);
                lower = capacitor(Vss, capacitance_value(lower_storage_unit.value * cascode_scale));
                positive = capacitor(tail, capacitance_value(steering_gate_unit.value * steering_scale));
                negative = capacitor(tail, capacitance_value(steering_gate_unit.value * steering_scale));
                positive = capacitor(P, capacitance_value(steering_miller_unit.value * steering_scale));
                negative = capacitor(N, capacitance_value(steering_miller_unit.value * steering_scale));
                bias = capacitor(Vss, capacitance_value(sink_gate_unit.value * weight));
                bias = capacitor(lower, capacitance_value(sink_miller_unit.value * weight));
                CASCODE_BIAS = capacitor(lower, capacitance_value(cascode_gate_unit.value * cascode_scale));
                CASCODE_BIAS = capacitor(tail, capacitance_value(cascode_miller_unit.value * cascode_scale));
            }
        }
    }
};

// The same core interface delivers steering and trim through real translators.
class cascoded_core_emphasized_pam5_driver
    : public basic_core_emphasized_pam5_driver<cascoded_emphasized_pam5_driver> {
    using Base = basic_core_emphasized_pam5_driver<cascoded_emphasized_pam5_driver>;
public:
    std::array<port<electrical>, 6> TRIM_N;
    std::array<clock_level_shifter, 6> trim_gates;

    void _assign() override {
        for (unsigned bit = 0; bit < TRIM_N.size(); ++bit)
            output.TRIM_N[bit] = trim_gates[bit].Y;
        Base::_assign();
        for (unsigned bit = 0; bit < TRIM_N.size(); ++bit) {
            auto& gate = trim_gates[bit];
            gate.A = TRIM_N[bit];
            gate.Vlogic = Vlogic;
            gate.Vdd = Vdd;
            gate.Vss = Vss;
            gate.BIAS = translation_bias.BIAS;
            gate.elaborate();
        }
    }
};

} // namespace analog::one_g::ihp_sg13g2
