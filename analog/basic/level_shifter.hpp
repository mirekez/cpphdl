#pragma once
#include "inverter.hpp"

namespace analog {

// A low-domain inverter supplies complementary gates to two HV pull-downs.
// Cross-coupled HV PMOS devices restore the high-domain output rails. Both
// branches remain connected while switching and draw real supply current.
// Two HV output inverters isolate the regenerative nodes from external loads.
class level_shifter : public IC {
public:
    _PORT(electrical) Vlogic;
    _PORT(electrical) A;
    _PORT(electrical) Y;
    _PORT(electrical) Y_N;
    electrical complement, translated, translated_n;
    cmos<channel_model> input_inverter;
    std::array<cmos<channel_model>, 2> output_inverter;
    mos_model logic_channel{channel_strength(.05), voltage(.35)};
    mos_model pull_down{channel_strength(.02), voltage(.7)};
    mos_model pull_up{channel_strength(.0005), voltage(.7)};
    mos_model output_channel{channel_strength(.01), voltage(.7)};
    capacitance_value logic_storage{.01e-12}, output_storage{.05e-12};
    capacitance_value gate_drain_storage{.002e-12};

    void _assign() override {
        input_inverter.A = A;
        input_inverter.Y = complement;
        input_inverter.Vdd = Vlogic;
        input_inverter.Vss = Vss;
        input_inverter.model.transistor_parameters = logic_channel;
        input_inverter.elaborate();
        complement = capacitor(Vss, logic_storage);
        translated = pmos(translated_n, Vdd, pull_up);
        translated_n = pmos(translated, Vdd, pull_up);
        Vss = nmos(complement, translated, pull_down);
        Vss = nmos(A, translated_n, pull_down);
        translated = capacitor(Vss, output_storage);
        translated_n = capacitor(Vss, output_storage);
        output_inverter[0].A = translated_n;
        output_inverter[0].Y = Y;
        output_inverter[1].A = translated;
        output_inverter[1].Y = Y_N;
        for (auto& buffer : output_inverter) {
            buffer.Vdd = Vdd;
            buffer.Vss = Vss;
            buffer.model.transistor_parameters = output_channel;
            buffer.elaborate();
        }
        Y = capacitor(Vss, output_storage);
        Y_N = capacitor(Vss, output_storage);
        A = capacitor(translated_n, gate_drain_storage);
        complement = capacitor(translated, gate_drain_storage);
    }
};
} // namespace analog
