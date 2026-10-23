#pragma once
#include "nand_latch.hpp"

namespace analog {

// Transparent-high storage with an asynchronous clear. Clear disables the
// transparent path and forces the stored state through actual MOS devices.
class cmos_d_latch : public IC {
public:
    _PORT(electrical) D;
    _PORT(electrical) ENABLE;
    _PORT(electrical) CLEAR;
    _PORT(electrical) Q;
    _PORT(electrical) Q_N;
    cmos<channel_model> data_inverter, clear_inverter, enable_inverter;
    cmos_nand qualify_enable, set_gate, reset_gate;
    cleared_nand_latch storage;
    mos_model channel{channel_strength(.01), voltage(.35)};
    capacitance_value node_storage{5e-15}, input_storage{2e-15};

    void _assign() override {
        data_inverter.A = D;
        clear_inverter.A = CLEAR;
        qualify_enable.A = ENABLE;
        qualify_enable.B = clear_inverter.Y;
        enable_inverter.A = qualify_enable.Y;
        set_gate.A = D;
        set_gate.B = enable_inverter.Y;
        reset_gate.A = data_inverter.Y;
        reset_gate.B = enable_inverter.Y;
        storage.SET_N = set_gate.Y;
        storage.RESET_N = reset_gate.Y;
        storage.CLEAR = CLEAR;
        storage.Q = Q;
        storage.Q_N = Q_N;
        for (auto* inverter : {&data_inverter, &clear_inverter, &enable_inverter}) {
            inverter->Vdd = Vdd;
            inverter->Vss = Vss;
            inverter->model.transistor_parameters = channel;
            inverter->elaborate();
            inverter->Y = capacitor(Vss, node_storage);
        }
        for (auto* gate : {&qualify_enable, &set_gate, &reset_gate}) {
            gate->Vdd = Vdd;
            gate->Vss = Vss;
            gate->parameters = channel;
            gate->output_capacitance = node_storage;
            gate->elaborate();
        }
        storage.Vdd = Vdd;
        storage.Vss = Vss;
        storage.channel = channel;
        storage.clear_channel = mos_model{channel_strength(channel.beta.value * 5), channel.threshold};
        storage.storage = node_storage;
        storage.input_storage = input_storage;
        storage.elaborate();
        D = capacitor(Vss, input_storage);
        ENABLE = capacitor(Vss, input_storage);
        CLEAR = capacitor(Vss, input_storage);
    }
};

// A low-transparent master and high-transparent slave transfer the stored
// value at the rising clock edge. Both latches share the actual supply rails.
class cmos_dff : public IC {
public:
    _PORT(electrical) D;
    _PORT(electrical) CLOCK;
    _PORT(electrical) CLEAR;
    _PORT(electrical) Q;
    _PORT(electrical) Q_N;
    cmos<channel_model> clock_inverter;
    cmos_d_latch master, slave;
    mos_model channel{channel_strength(.01), voltage(.35)};
    capacitance_value node_storage{5e-15}, input_storage{2e-15};

    void _assign() override {
        clock_inverter.A = CLOCK;
        clock_inverter.Vdd = Vdd;
        clock_inverter.Vss = Vss;
        clock_inverter.model.transistor_parameters = channel;
        clock_inverter.elaborate();
        clock_inverter.Y = capacitor(Vss, node_storage);
        master.D = D;
        master.ENABLE = clock_inverter.Y;
        slave.D = master.Q;
        slave.ENABLE = CLOCK;
        slave.Q = Q;
        slave.Q_N = Q_N;
        for (auto* latch : {&master, &slave}) {
            latch->CLEAR = CLEAR;
            latch->Vdd = Vdd;
            latch->Vss = Vss;
            latch->channel = channel;
            latch->node_storage = node_storage;
            latch->input_storage = input_storage;
            latch->elaborate();
        }
    }
};

// Feedback is an electrical wire from Q_N to D. This divider contains no
// host counter and does not impose a period on the output waveform.
class cmos_divide2 : public IC {
public:
    _PORT(electrical) CLOCK;
    _PORT(electrical) CLEAR;
    _PORT(electrical) Q;
    _PORT(electrical) Q_N;
    cmos_dff storage;

    void _assign() override {
        storage.D = Q_N;
        storage.CLOCK = CLOCK;
        storage.CLEAR = CLEAR;
        storage.Q = Q;
        storage.Q_N = Q_N;
        storage.Vdd = Vdd;
        storage.Vss = Vss;
        storage.elaborate();
    }
};
} // namespace analog
