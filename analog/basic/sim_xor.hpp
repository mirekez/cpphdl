#pragma once
#include "gm_stage.hpp"
#include "sim_clocked_storage.hpp"

namespace analog {
// A finite-drive Boolean approximation composed from a buffered inverter and a mux.
class sim_xor : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) B;
    _PORT(electrical) Y;
    gm_stage inverted;
    sim_mux2 select;

    void _assign() override {
        inverted.PLUS = Vss;
        inverted.MINUS = A;
        inverted.BIAS = Vdd;
        inverted.gain = transconductance(.02);
        inverted.output_resistance = resistance(50);
        inverted.storage = capacitance_value(1e-15);
        inverted.Vdd = Vdd;
        inverted.Vss = Vss;
        inverted.elaborate();
        select.A = A;
        select.B = inverted.OUT;
        select.SELECT = B;
        select.Y = Y;
        select.Vdd = Vdd;
        select.Vss = Vss;
        select.elaborate();
    }
};
} // namespace analog
