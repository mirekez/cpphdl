#pragma once
#include <analog.hpp>

namespace analog {
// Finite-conductance switch/storage approximation. Every state is capacitor charge.
class sim_sample_hold : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) CLK;
    _PORT(electrical) OUT;
    resistance on_resistance{25};
    resistance leakage{1e9};
    capacitance_value storage{5e-15};
    voltage initial{0};
    control_response response = control_response::rising;
    voltage low{.75}, high{.95};

    void _assign() override {
        OUT = sim_buffered_track(IN, CLK, Vss, on_resistance, low, high, response);
        OUT = capacitor(Vss, storage, initial);
        Vss = resistor(OUT, leakage);
    }
};

class sim_dff : public IC {
public:
    _PORT(electrical) D;
    _PORT(electrical) CLK;
    _PORT(electrical) Q;
    sim_sample_hold master, slave;
    bool falling_edge = false;

    void _assign() override {
        master.IN = D;
        master.CLK = CLK;
        master.response = falling_edge ? control_response::rising : control_response::falling;
        master.low = voltage(falling_edge ? .75 : .25);
        master.high = voltage(falling_edge ? .95 : .45);
        slave.response = falling_edge ? control_response::falling : control_response::rising;
        slave.low = voltage(falling_edge ? .25 : .75);
        slave.high = voltage(falling_edge ? .45 : .95);
        slave.IN = master.OUT;
        slave.CLK = CLK;
        slave.OUT = Q;
        for (auto* latch : {&master, &slave}) {
            latch->Vdd = Vdd;
            latch->Vss = Vss;
            latch->elaborate();
        }
    }
};

class sim_mux2 : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) B;
    _PORT(electrical) SELECT;
    _PORT(electrical) Y;
    resistance drive{25};
    capacitance_value storage{2e-15};

    void _assign() override {
        Y = sim_buffered_track(A, SELECT, Vss, drive, voltage(.25), voltage(.45), control_response::falling);
        Y = sim_buffered_track(B, SELECT, Vss, drive, voltage(.75), voltage(.95), control_response::rising);
        Y = capacitor(Vss, storage);
        Vss = resistor(Y, resistance(1e9));
    }
};
} // namespace analog
