#pragma once
#include <analog.hpp>

namespace analog {
class cmos_nand : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) B;
    _PORT(electrical) Y;
    electrical STACK;
    capacitance_value output_capacitance{1e-9};
    capacitance_value stack_capacitance{10e-15};
    bool weak_returns{true};
    mos_model parameters{channel_strength(.002), voltage(.35)};

    void _assign() override {
        Y = capacitor(Vss, output_capacitance); // Explicit load charge makes transitions finite.
        Y = pmos(A, Vdd, parameters);
        Y = pmos(B, Vdd, parameters); // Either low input enables a pull-up path.
        STACK = nmos(A, Y, parameters);
        Vss = nmos(B, STACK, parameters); // Both high inputs complete the pull-down path.
        if (weak_returns) {
            Vss = resistor(Y, resistance(1e9));
            Vss = resistor(STACK, resistance(1e9));
        } else {
            // The isolated stack retains charge until a channel conducts.
            STACK = capacitor(Vss, stack_capacitance);
        }
    }
};

class cmos_xor : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) B;
    _PORT(electrical) Y;
    cmos_nand common, left, right, output;

    void _assign() override {
        common.A = A;
        common.B = B;
        left.A = A;
        left.B = common.Y;
        right.A = B;
        right.B = common.Y;
        output.A = left.Y;
        output.B = right.Y;
        output.Y = Y;
        for (auto* gate : {&common, &left, &right, &output}) {
            gate->Vdd = Vdd;
            gate->Vss = Vss;
            gate->elaborate();
        }
    }
};
} // namespace analog
