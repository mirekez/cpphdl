#pragma once
#include <analog.hpp>
#include <array>

namespace analog {
class current_starved_inverter : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;
    _PORT(electrical) CONTROL;
    electrical TAIL;
    mos_model switch_parameters{channel_strength(.001), voltage(.35)};
    mos_model current_parameters{channel_strength(.001), voltage(.35)};
    capacitance_value stage_capacitance{1e-9};
    voltage initial{0};

    void _assign() override {
        Y = pmos(A, Vdd, switch_parameters);
        TAIL = nmos(A, Y, switch_parameters);
        Vss = nmos(CONTROL, TAIL, current_parameters); // Control limits discharge current and thus propagation time.
        Vss = resistor(TAIL, resistance(1e9));
        Y = capacitor(Vss, stage_capacitance, initial); // Propagation delay comes from stored charge.
    }
};

class ring_vco : public IC {
public:
    _PORT(electrical) CONTROL;
    _PORT(electrical) OUT;
    std::array<current_starved_inverter, 3> stages;

    void _assign() override {
        stages[2].Y = OUT;
        for (std::size_t i = 0; i < stages.size(); ++i) {
            stages[i].A = stages[(i + 2) % 3].Y;
            stages[i].CONTROL = CONTROL;
            stages[i].Vdd = Vdd;
            stages[i].Vss = Vss;
            stages[i].elaborate();
        }
    }
};
} // namespace analog
