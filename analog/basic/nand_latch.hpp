#pragma once
#include "cmos_logic.hpp"
#include "inverter.hpp"
#include <array>

namespace analog {

// Active-low S/R latch. Both inputs high retain the previous state. A low
// SET_N enables Q; a low RESET_N disables it. Both low assert both outputs.
class nand_latch : public IC {
public:
    _PORT(electrical) SET_N;
    _PORT(electrical) RESET_N;
    _PORT(electrical) Q;
    _PORT(electrical) Q_N;
    std::array<cmos_nand, 2> gates;
    capacitance_value storage{.05e-12}, input_storage{.003e-12};
    mos_model channel{channel_strength(.01), voltage(.7)};
    bool weak_returns{true};

    void _assign() override {
        gates[0].A = SET_N;
        gates[0].B = Q_N;
        gates[0].Y = Q;
        gates[1].A = RESET_N;
        gates[1].B = Q;
        gates[1].Y = Q_N;
        for (auto& gate : gates) {
            gate.Vdd = Vdd;
            gate.Vss = Vss;
            gate.parameters = channel;
            gate.output_capacitance = storage;
            gate.weak_returns = weak_returns;
            gate.elaborate();
        }
        SET_N = capacitor(Vss, input_storage);
        RESET_N = capacitor(Vss, input_storage);
    }
};

// Power-up clear is applied while the S/R inputs are inactive. These added
// devices force the complementary outputs before the first conversion.
class cleared_nand_latch : public nand_latch {
public:
    _PORT(electrical) CLEAR;
    cmos<channel_model> clear_inverter;
    mos_model clear_channel{channel_strength(.05), voltage(.7)};

    void _assign() override {
        nand_latch::_assign();
        clear_inverter.A = CLEAR;
        clear_inverter.Vdd = Vdd;
        clear_inverter.Vss = Vss;
        clear_inverter.model.transistor_parameters = channel;
        clear_inverter.elaborate();
        // The inverter drives the added PMOS gate. Its explicit gate/wire
        // storage also retains this voltage across an integration pause.
        clear_inverter.Y = capacitor(Vss, input_storage);
        Vss = nmos(CLEAR, Q, clear_channel);
        Q_N = pmos(clear_inverter.Y, Vdd, clear_channel);
    }
};
} // namespace analog
