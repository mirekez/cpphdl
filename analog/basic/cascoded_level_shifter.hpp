#pragma once
#include "inverter.hpp"

namespace analog {

// A replica HV diode biases cascodes from a fraction of the logic supply.
// The bias is an achieved voltage: all attached gate charge loads this node.
class level_shift_bias : public IC {
public:
    _PORT(electrical) Vlogic;
    _PORT(electrical) BIAS;
    electrical DIVIDED;
    resistance top{200}, bottom{800}, feed{10000};
    capacitance_value storage{1e-12};
    mos_model replica{channel_strength(.01875), voltage(.7)};

    void _assign() override {
        DIVIDED = resistor(Vlogic, top);
        DIVIDED = resistor(Vss, bottom);
        BIAS = resistor(Vdd, feed);
        DIVIDED = nmos(BIAS, BIAS, replica);
        BIAS = capacitor(Vss, storage);
    }
};

// Low-voltage input devices pull through HV cascodes instead of asking
// low-swing gates to switch HV devices directly. The caller supplies BIAS,
// allowing multiple translators to share one physical replica-bias network.
class cascoded_level_shifter : public IC {
public:
    _PORT(electrical) Vlogic;
    _PORT(electrical) BIAS;
    _PORT(electrical) A;
    _PORT(electrical) Y;
    _PORT(electrical) Y_N;
    electrical complement, translated, translated_n, low_node, low_node_n;
    cmos<channel_model> input_inverter;
    std::array<cmos<channel_model>, 2> output_inverter;
    mos_model logic_channel{channel_strength(.02), voltage(.5)};
    mos_model low_pull_down{channel_strength(.06), voltage(.5)};
    mos_model cascode{channel_strength(.015), voltage(.7)};
    mos_model pull_up{channel_strength(.0014), voltage(.7)};
    mos_model output_channel{channel_strength(.003), voltage(.7)};
    capacitance_value logic_storage{10e-15}, translated_storage{10e-15};
    capacitance_value low_storage{10e-15}, output_storage{50e-15};
    capacitance_value logic_gate_storage{40e-15}, gate_drain_storage{16e-15};
    capacitance_value cascode_gate_source_storage{60e-15}, cascode_gate_drain_storage{36e-15};
    capacitance_value buffer_gate_storage{70e-15}, buffer_miller_storage{23e-15};
    capacitance_value latch_gate_storage{40e-15}, latch_miller_storage{14e-15};

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
        low_node = nmos(BIAS, translated, cascode);
        low_node_n = nmos(BIAS, translated_n, cascode);
        Vss = nmos(complement, low_node, low_pull_down);
        Vss = nmos(A, low_node_n, low_pull_down);
        translated = capacitor(Vss, translated_storage);
        translated_n = capacitor(Vss, translated_storage);
        low_node = capacitor(Vss, low_storage);
        low_node_n = capacitor(Vss, low_storage);
        // Gate charge returns to the actual bias, supply and signal nodes.
        // A shared replica must therefore support simultaneous translator
        // switching; its voltage is not an isolated control parameter.
        BIAS = capacitor(low_node, cascode_gate_source_storage);
        BIAS = capacitor(low_node_n, cascode_gate_source_storage);
        BIAS = capacitor(translated, cascode_gate_drain_storage);
        BIAS = capacitor(translated_n, cascode_gate_drain_storage);
        A = capacitor(Vss, logic_gate_storage);
        complement = capacitor(Vss, logic_gate_storage);
        translated = capacitor(Vdd, latch_gate_storage);
        translated_n = capacitor(Vdd, latch_gate_storage);
        translated = capacitor(translated_n, latch_miller_storage);
        translated = capacitor(Vss, buffer_gate_storage);
        translated_n = capacitor(Vss, buffer_gate_storage);
        translated = capacitor(Y_N, buffer_miller_storage);
        translated_n = capacitor(Y, buffer_miller_storage);
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
        A = capacitor(low_node_n, gate_drain_storage);
        complement = capacitor(low_node, gate_drain_storage);
    }
};
} // namespace analog
