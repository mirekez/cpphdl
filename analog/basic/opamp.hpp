#pragma once
#include "nmos_differential_pair.hpp"
#include "pmos_current_mirror.hpp"

namespace analog {

// A single-stage differential amplifier; feedback, loading, and bias set its behavior.
class opamp : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) OUT;
    electrical TAIL, MIRROR;
    nmos_differential_pair input;
    pmos_current_mirror load;
    resistance tail_resistance{10};
    resistance output_shunt{100000};
    capacitance_value compensation{1e-8};

    voltage initial_output{0};

    void _assign() override {
        input.PLUS = PLUS;
        input.MINUS = MINUS;
        input.LEFT = MIRROR;
        input.RIGHT = OUT;
        input.TAIL = TAIL;
        input.Vdd = Vdd;
        input.Vss = Vss;
        input.elaborate();
        Vss = resistor(TAIL, tail_resistance); // Return both input currents through the bias resistor.

        load.IN = MIRROR;
        load.OUT = OUT;
        load.Vdd = Vdd;
        load.Vss = Vss;
        load.elaborate(); // Mirror left-branch current into the right/output branch.

        OUT = capacitor(Vss, compensation, initial_output); // Store output charge explicitly.
        Vss = resistor(OUT, output_shunt); // Finite shunt conductance bounds the unloaded gain.
    }
};

} // namespace analog
