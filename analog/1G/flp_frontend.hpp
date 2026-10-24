#pragma once
#include <basic/inverter.hpp>
#include <basic/opamp.hpp>

namespace analog::one_g {

// A unipolar link-pulse driver with explicit output resistance. ENABLE_N is
// made inside the circuit; disabling opens both output transmission gates.
// Electrical gate inputs use the I/O rail. A core-domain source needs a level
// interface, and a chip implementation needs process devices and magnetics.
class flp_pulse_driver : public IC {
public:
    _PORT(electrical) COMMAND;
    _PORT(electrical) ENABLE;
    _PORT(electrical) P;
    _PORT(electrical) N;
    electrical COMMAND_N, ENABLE_N, DRIVE, SWITCH_P, SWITCH_N;
    resistance output_resistance{20};
    capacitance_value gate_storage{20e-15}, output_storage{.5e-12};
    mos_model output_channel{channel_strength(.1), voltage(.7)};
    mos_model logic_channel{channel_strength(.01), voltage(.7)};

    void _assign() override {
        COMMAND_N = pmos(COMMAND, Vdd, logic_channel);
        Vss = nmos(COMMAND, COMMAND_N, logic_channel);
        COMMAND_N = capacitor(Vss, gate_storage);
        ENABLE_N = pmos(ENABLE, Vdd, logic_channel);
        Vss = nmos(ENABLE, ENABLE_N, logic_channel);
        ENABLE_N = capacitor(Vss, gate_storage);
        DRIVE = pmos(COMMAND_N, Vdd, output_channel);
        Vss = nmos(COMMAND_N, DRIVE, output_channel);
        DRIVE = capacitor(Vss, gate_storage);
        SWITCH_P = pmos(ENABLE_N, DRIVE, output_channel);
        SWITCH_P = nmos(ENABLE, DRIVE, output_channel);
        SWITCH_N = pmos(ENABLE_N, Vss, output_channel);
        SWITCH_N = nmos(ENABLE, Vss, output_channel);
        P = resistor(SWITCH_P, output_resistance);
        N = resistor(SWITCH_N, output_resistance);
        P = capacitor(Vss, output_storage);
        N = capacitor(Vss, output_storage);
        // Leakage defines disabled internal nodes without imposing a voltage.
        SWITCH_P = resistor(Vss, resistance(1e9));
        SWITCH_N = resistor(Vss, resistance(1e9));
    }
};

// Two transistor differential amplifiers detect either pulse polarity. The
// resistor networks create a finite-impedance differential dead band; loading
// changes its actual threshold. The NOR and output inverter restore rail levels.
class flp_pulse_detector : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) PULSE;
    electrical LOW, HIGH, PLUS[2], MINUS[2], STACK, NOR;
    opamp comparators[2];
    mos_model logic_channel{channel_strength(.01), voltage(.7)};
    capacitance_value input_storage{3e-15}, output_storage{20e-15};

    void _assign() override {
        LOW = resistor(Vdd, resistance(7000));
        LOW = resistor(Vss, resistance(5000));
        HIGH = resistor(Vdd, resistance(5000));
        HIGH = resistor(Vss, resistance(7000));
        LOW = capacitor(Vss, capacitance_value(.1e-12));
        HIGH = capacitor(Vss, capacitance_value(.1e-12));
        for (unsigned i = 0; i < 2; ++i) {
            PLUS[i] = resistor(i == 0 ? P : N, resistance(100000));
            PLUS[i] = resistor(LOW, resistance(100000));
            MINUS[i] = resistor(i == 0 ? N : P, resistance(100000));
            MINUS[i] = resistor(HIGH, resistance(100000));
            PLUS[i] = capacitor(Vss, input_storage);
            MINUS[i] = capacitor(Vss, input_storage);
            auto& amplifier = comparators[i];
            amplifier.PLUS = PLUS[i];
            amplifier.MINUS = MINUS[i];
            amplifier.Vdd = Vdd;
            amplifier.Vss = Vss;
            amplifier.tail_resistance = resistance(500);
            amplifier.output_shunt = resistance(1000000);
            amplifier.compensation = capacitance_value(.05e-12);
            amplifier.input.model = mos_model{channel_strength(.001), voltage(.35)};
            amplifier.load.model = mos_model{channel_strength(.001), voltage(.35)};
            amplifier.elaborate();
        }
        STACK = pmos(comparators[0].OUT, Vdd, logic_channel);
        NOR = pmos(comparators[1].OUT, STACK, logic_channel);
        Vss = nmos(comparators[0].OUT, NOR, logic_channel);
        Vss = nmos(comparators[1].OUT, NOR, logic_channel);
        STACK = resistor(Vss, resistance(1e9));
        NOR = capacitor(Vss, output_storage);
        PULSE = pmos(NOR, Vdd, logic_channel);
        Vss = nmos(NOR, PULSE, logic_channel);
        PULSE = capacitor(Vss, output_storage);
    }
};
} // namespace analog::one_g
