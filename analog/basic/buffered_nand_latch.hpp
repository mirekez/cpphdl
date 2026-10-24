#pragma once
#include "nand_latch.hpp"

namespace analog {

// Preserve active-low latch commands while isolating their sources from
// state-dependent NAND input charge. Both inverter stages draw real rail current.
class buffered_nand_latch : public IC {
public:
    _PORT(electrical) SET_N;
    _PORT(electrical) RESET_N;
    _PORT(electrical) CLEAR;
    _PORT(electrical) Q;
    _PORT(electrical) Q_N;
    std::array<cmos<channel_model>, 2> input, output;
    cleared_nand_latch storage;
    capacitance_value input_storage{2e-15}, miller_storage{1e-15};
    capacitance_value intermediate_storage{5e-15}, output_input_storage{8e-15}, output_storage{10e-15};
    bool weak_returns{true};

    void _assign() override {
        input[0].A = SET_N;
        input[1].A = RESET_N;
        for (unsigned i = 0; i < 2; ++i) {
            input[i].model.transistor_parameters = mos_model{channel_strength(.001), voltage(.7)};
            output[i].model.transistor_parameters = mos_model{channel_strength(.004), voltage(.7)};
            input[i].Vdd = Vdd;
            input[i].Vss = Vss;
            output[i].A = input[i].Y;
            output[i].Vdd = Vdd;
            output[i].Vss = Vss;
            input[i].elaborate();
            output[i].elaborate();
            input[i].A = capacitor(Vss, input_storage);
            input[i].A = capacitor(input[i].Y, miller_storage);
            input[i].Y = capacitor(Vss, intermediate_storage);
            output[i].A = capacitor(Vss, output_input_storage);
            output[i].Y = capacitor(Vss, output_storage);
        }
        storage.SET_N = output[0].Y;
        storage.RESET_N = output[1].Y;
        storage.CLEAR = CLEAR;
        storage.Q = Q;
        storage.Q_N = Q_N;
        storage.Vdd = Vdd;
        storage.Vss = Vss;
        storage.weak_returns = weak_returns;
        storage.elaborate();
    }
};
} // namespace analog
