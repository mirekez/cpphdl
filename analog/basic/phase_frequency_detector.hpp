#pragma once
#include "cmos_storage.hpp"

namespace analog {

// The first arriving edge sets its corresponding flip-flop. The other edge
// completes a common asynchronous reset. Unlike an XOR detector, missing or
// slower feedback leaves UP asserted and cannot establish a subharmonic lock.
class cmos_phase_frequency_detector : public IC {
public:
    _PORT(electrical) REF;
    _PORT(electrical) FEEDBACK;
    _PORT(electrical) CLEAR;
    _PORT(electrical) UP;
    _PORT(electrical) UP_N;
    _PORT(electrical) DOWN;
    _PORT(electrical) DOWN_N;
    cmos_dff reference, feedback;
    cmos_nand both, reset;
    cmos<channel_model> clear_inverter;
    electrical RESET;
    mos_model channel{channel_strength(.01), voltage(.35)};
    capacitance_value node_storage{5e-15};
    resistance reset_resistance{1000};
    capacitance_value reset_storage{100e-15};

    void _assign() override {
        reference.D = Vdd;
        reference.CLOCK = REF;
        reference.Q = UP;
        reference.Q_N = UP_N;
        feedback.D = Vdd;
        feedback.CLOCK = FEEDBACK;
        feedback.Q = DOWN;
        feedback.Q_N = DOWN_N;
        both.A = UP;
        both.B = DOWN;
        clear_inverter.A = CLEAR;
        clear_inverter.Vdd = Vdd;
        clear_inverter.Vss = Vss;
        clear_inverter.model.transistor_parameters = channel;
        clear_inverter.elaborate();
        clear_inverter.Y = capacitor(Vss, node_storage);
        reset.A = both.Y;
        reset.B = clear_inverter.Y;
        for (auto* gate : {&both, &reset}) {
            gate->Vdd = Vdd;
            gate->Vss = Vss;
            gate->parameters = channel;
            gate->output_capacitance = node_storage;
            gate->elaborate();
        }
        for (auto* storage : {&reference, &feedback}) {
            storage->CLEAR = RESET;
            storage->Vdd = Vdd;
            storage->Vss = Vss;
            storage->channel = channel;
            storage->node_storage = node_storage;
            storage->elaborate();
        }
        // A finite common-reset pulse avoids an arbitrarily narrow reset
        // window and lets both master/slave storage paths actually clear.
        RESET = resistor(reset.Y, reset_resistance);
        RESET = capacitor(Vss, reset_storage);
    }
};
} // namespace analog
