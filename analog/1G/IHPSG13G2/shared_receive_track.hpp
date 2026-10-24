#pragma once
#include "../clock_level_shifter.hpp"
#include <basic/nand_latch.hpp>
#include <basic/mos_track_hold.hpp>

namespace analog::one_g::ihp_sg13g2 {

// A physical rising-event window. Request selections may change after this
// pulse ends even while the slower shared translator output is still high.
// This does not advance the closing aperture or replace a clock phase plan.
class receive_track_event : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    _PORT(electrical) DELAY;
    _PORT(electrical) DELAY_N;
    _PORT(electrical) NAND_OUT;
    cmos<channel_model> delayed_inverter, driver;
    cmos_nand qualify;

    void _assign() override {
        DELAY = resistor(IN, resistance(1000));
        DELAY = capacitor(Vss, capacitance_value(1e-12));
        delayed_inverter.A = DELAY;
        delayed_inverter.Y = DELAY_N;
        delayed_inverter.Vdd = Vdd;
        delayed_inverter.Vss = Vss;
        delayed_inverter.model.transistor_parameters = mos_model{channel_strength(.004), voltage(.7)};
        delayed_inverter.elaborate();
        DELAY_N = capacitor(Vss, capacitance_value(10e-15));
        qualify.A = IN;
        qualify.B = DELAY_N;
        qualify.Y = NAND_OUT;
        qualify.Vdd = Vdd;
        qualify.Vss = Vss;
        qualify.parameters = mos_model{channel_strength(.01), voltage(.7)};
        qualify.output_capacitance = capacitance_value(10e-15);
        qualify.weak_returns = false;
        qualify.elaborate();
        driver.A = NAND_OUT;
        driver.Y = OUT;
        driver.Vdd = Vdd;
        driver.Vss = Vss;
        driver.model.transistor_parameters = mos_model{channel_strength(.006), voltage(.7)};
        driver.elaborate();
        OUT = capacitor(Vss, capacitance_value(20e-15));
    }
};

// Y = !(ENABLE && (PRECHARGE || STROBE && OPEN && CLOSE_N && ALLOW)).
// Closing selection is established before the strobe, so simultaneous open
// and close requests suppress SET throughout the event, including its release.
class receive_track_set_gate : public IC {
public:
    _PORT(electrical) ENABLE;
    _PORT(electrical) PRECHARGE;
    _PORT(electrical) STROBE;
    _PORT(electrical) OPEN;
    _PORT(electrical) CLOSE_N;
    _PORT(electrical) ALLOW;
    _PORT(electrical) Y;
    std::array<electrical, 4> down;
    electrical up;
    mos_model channel{channel_strength(.008), voltage(.7)};
    capacitance_value node_storage{5e-15};

    void _assign() override {
        Y = pmos(ENABLE, Vdd, channel);
        up = pmos(PRECHARGE, Vdd, channel);
        Y = pmos(STROBE, up, channel);
        Y = pmos(OPEN, up, channel);
        Y = pmos(CLOSE_N, up, channel);
        Y = pmos(ALLOW, up, channel);
        down[0] = nmos(ENABLE, Y, channel);
        Vss = nmos(PRECHARGE, down[0], channel);
        down[1] = nmos(STROBE, down[0], channel);
        down[2] = nmos(OPEN, down[1], channel);
        down[3] = nmos(CLOSE_N, down[2], channel);
        Vss = nmos(ALLOW, down[3], channel);
        Y = capacitor(Vss, node_storage);
        up = capacitor(Vss, node_storage);
        for (auto& node : down)
            node = capacitor(Vss, node_storage);
    }
};

// Y = !(DISABLE || STROBE && CLOSE && PRECHARGE_N).
class receive_track_reset_gate : public IC {
public:
    _PORT(electrical) DISABLE;
    _PORT(electrical) PRECHARGE_N;
    _PORT(electrical) STROBE;
    _PORT(electrical) CLOSE;
    _PORT(electrical) Y;
    std::array<electrical, 2> down;
    electrical up;
    mos_model channel{channel_strength(.008), voltage(.7)};
    capacitance_value node_storage{5e-15};

    void _assign() override {
        up = pmos(DISABLE, Vdd, channel);
        Y = pmos(STROBE, up, channel);
        Y = pmos(CLOSE, up, channel);
        Y = pmos(PRECHARGE_N, up, channel);
        Vss = nmos(DISABLE, Y, channel);
        down[0] = nmos(STROBE, Y, channel);
        down[1] = nmos(CLOSE, down[0], channel);
        Vss = nmos(PRECHARGE_N, down[1], channel);
        Y = capacitor(Vss, node_storage);
        up = capacitor(Vss, node_storage);
        for (auto& node : down)
            node = capacitor(Vss, node_storage);
    }
};

class receive_track_state : public IC {
public:
    _PORT(electrical) ENABLE;
    _PORT(electrical) DISABLE;
    _PORT(electrical) PRECHARGE;
    _PORT(electrical) PRECHARGE_N;
    _PORT(electrical) STROBE;
    _PORT(electrical) OPEN;
    _PORT(electrical) CLOSE;
    _PORT(electrical) CLOSE_N;
    _PORT(electrical) ALLOW;
    _PORT(electrical) TRACK;
    _PORT(electrical) STATE_N;
    _PORT(electrical) SET_N;
    _PORT(electrical) RESET_N;
    receive_track_set_gate set;
    receive_track_reset_gate reset;
    nand_latch memory;

    void _assign() override {
        set.ENABLE = ENABLE;
        set.PRECHARGE = PRECHARGE;
        set.STROBE = STROBE;
        set.OPEN = OPEN;
        set.CLOSE_N = CLOSE_N;
        set.ALLOW = ALLOW;
        set.Y = SET_N;
        set.Vdd = Vdd;
        set.Vss = Vss;
        set.elaborate();
        reset.DISABLE = DISABLE;
        reset.PRECHARGE_N = PRECHARGE_N;
        reset.STROBE = STROBE;
        reset.CLOSE = CLOSE;
        reset.Y = RESET_N;
        reset.Vdd = Vdd;
        reset.Vss = Vss;
        reset.elaborate();
        memory.SET_N = SET_N;
        memory.RESET_N = RESET_N;
        memory.Q = TRACK;
        memory.Q_N = STATE_N;
        memory.Vdd = Vdd;
        memory.Vss = Vss;
        memory.weak_returns = false;
        memory.elaborate();
    }
};

// Public probes keep the fixture out of grandchildren's internal wires.
class receive_track_translator : public one_g::clock_level_shifter {
public:
    _PORT(electrical) INPUT_N;
    _PORT(electrical) LOW;
    _PORT(electrical) LOW_N;
    bool shared_strobe{false};

    void _assign() override {
        INPUT_N = complement;
        LOW = low_node;
        LOW_N = low_node_n;
        if (shared_strobe) {
            output_channel = mos_model{channel_strength(.006), voltage(.7)};
            pull_up = mos_model{channel_strength(.0028), voltage(.7)};
        }
        one_g::clock_level_shifter::_assign();
    }
};

// Same main/dummy/complement/storage profile as the qualified balanced sampler.
// This unit intentionally excludes its DAC/preamp and therefore cannot qualify
// their held-node kickback or the complete conversion path.
class receive_track_sampler_load : public mos_track_hold {
public:
    _PORT(electrical) CLOCK_N;
    void _assign() override {
        complement.Y = CLOCK_N;
        n_channel = mos_model{channel_strength(.03), voltage(.7)};
        p_channel = mos_model{channel_strength(.015), voltage(.7)};
        dummy_n = mos_model{channel_strength(.016), voltage(.7)};
        dummy_p = mos_model{channel_strength(.0084), voltage(.7)};
        storage = capacitance_value(1.5e-12);
        clock_storage = capacitance_value(20e-15);
        leakage_return = false;
        initial_input = voltage(0);
        initial_supply = voltage(0);
        mos_track_hold::_assign();
    }
};

class shared_receive_track : public IC {
public:
    static constexpr unsigned slices = 13;
    static constexpr unsigned interfaces = slices + 4;
    _PORT(electrical) Vlogic;
    _PORT(electrical) IN;
    _PORT(electrical) STROBE_CORE;
    _PORT(electrical) ENABLE_CORE;
    _PORT(electrical) PRECHARGE_CORE;
    _PORT(electrical) OPEN_ALLOW_CORE;
    _PORT(electrical) BIAS;
    _PORT(electrical) STROBE;
    _PORT(electrical) EVENT;
    _PORT(electrical) EVENT_DELAY;
    _PORT(electrical) EVENT_DELAY_N;
    _PORT(electrical) EVENT_NAND;
    std::array<port<electrical>, slices> SELECT_CORE, TRACK, CLOCK_N, HELD;
    std::array<port<electrical>, slices> STATE_N, SET_N, RESET_N;
    std::array<port<electrical>, interfaces> CONTROL, INPUT_N, LOW, LOW_N;
    std::array<receive_track_translator, interfaces> translator;
    std::array<receive_track_state, slices> state;
    std::array<receive_track_sampler_load, slices> sampler;
    level_shift_bias bias;
    receive_track_event aperture_event;

    void _assign() override {
        bias.Vdd = Vdd;
        bias.Vlogic = Vlogic;
        bias.Vss = Vss;
        bias.BIAS = BIAS;
        bias.storage = capacitance_value(25e-12);
        bias.elaborate();
        for (unsigned i = 0; i < interfaces; ++i) {
            if (i < slices)
                CONTROL[i] = SELECT_CORE[i];
            else if (i == slices)
                CONTROL[i] = STROBE_CORE;
            else if (i == slices + 1)
                CONTROL[i] = ENABLE_CORE;
            else if (i == slices + 2)
                CONTROL[i] = PRECHARGE_CORE;
            else
                CONTROL[i] = OPEN_ALLOW_CORE;
            translator[i].A = CONTROL[i];
            translator[i].Vdd = Vdd;
            translator[i].Vlogic = Vlogic;
            translator[i].Vss = Vss;
            translator[i].BIAS = BIAS;
            translator[i].INPUT_N = INPUT_N[i];
            translator[i].LOW = LOW[i];
            translator[i].LOW_N = LOW_N[i];
            translator[i].shared_strobe = i == slices;
            if (i == slices)
                translator[i].Y = STROBE;
            translator[i].elaborate();
        }
        aperture_event.IN = STROBE;
        aperture_event.OUT = EVENT;
        aperture_event.DELAY = EVENT_DELAY;
        aperture_event.DELAY_N = EVENT_DELAY_N;
        aperture_event.NAND_OUT = EVENT_NAND;
        aperture_event.Vdd = Vdd;
        aperture_event.Vss = Vss;
        aperture_event.elaborate();
        for (unsigned i = 0; i < slices; ++i) {
            state[i].ENABLE = translator[slices + 1].Y;
            state[i].DISABLE = translator[slices + 1].Y_N;
            state[i].PRECHARGE = translator[slices + 2].Y;
            state[i].PRECHARGE_N = translator[slices + 2].Y_N;
            state[i].STROBE = EVENT;
            state[i].OPEN = translator[(i + slices - 2) % slices].Y;
            state[i].CLOSE = translator[i].Y;
            state[i].CLOSE_N = translator[i].Y_N;
            state[i].ALLOW = translator[slices + 3].Y;
            state[i].TRACK = TRACK[i];
            state[i].STATE_N = STATE_N[i];
            state[i].SET_N = SET_N[i];
            state[i].RESET_N = RESET_N[i];
            state[i].Vdd = Vdd;
            state[i].Vss = Vss;
            state[i].elaborate();
            sampler[i].IN = IN;
            sampler[i].CLOCK = TRACK[i];
            sampler[i].CLOCK_N = CLOCK_N[i];
            sampler[i].OUT = HELD[i];
            sampler[i].Vdd = Vdd;
            sampler[i].Vss = Vss;
            sampler[i].elaborate();
        }
    }
};

} // namespace analog::one_g::ihp_sg13g2
