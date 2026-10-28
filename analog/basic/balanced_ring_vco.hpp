#pragma once
#include <analog.hpp>
#include <array>

namespace analog {
// Both transition directions receive a limited current. A shared diode PMOS
// mirrors the control-dependent NMOS sink into the three charging branches.
class balanced_starved_inverter : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;
    _PORT(electrical) CONTROL;
    _PORT(electrical) CHARGE_BIAS;
    electrical HEAD, TAIL;
    mos_model switch_parameters{channel_strength(.001), voltage(.35)};
    mos_model current_parameters{channel_strength(.001), voltage(.35)};
    capacitance_value stage_capacitance{1e-9};
    capacitance_value internal_capacitance{1e-15};
    voltage initial{0};

    void _assign() override {
        HEAD = pmos(CHARGE_BIAS, Vdd, current_parameters);
        Y = pmos(A, HEAD, switch_parameters);
        TAIL = nmos(A, Y, switch_parameters);
        Vss = nmos(CONTROL, TAIL, current_parameters);
        // Retain internal charge when a current-limiting device turns off.
        // These explicit storage elements also define the initial transient.
        TAIL = capacitor(Vss, internal_capacitance);
        HEAD = capacitor(Vdd, internal_capacitance);
        Y = capacitor(Vss, stage_capacitance, initial);
    }
};

class balanced_ring_vco : public IC {
public:
    _PORT(electrical) CONTROL;
    _PORT(electrical) OUT;
    electrical CHARGE_BIAS;
    std::array<balanced_starved_inverter, 3> stages;
    mos_model mirror_parameters{channel_strength(.001), voltage(.35)};
    capacitance_value bias_capacitance{1e-15};

    void _assign() override {
        CHARGE_BIAS = pmos(CHARGE_BIAS, Vdd, mirror_parameters);
        Vss = nmos(CONTROL, CHARGE_BIAS, mirror_parameters);
        CHARGE_BIAS = capacitor(Vdd, bias_capacitance);
        stages[2].Y = OUT;
        for (std::size_t i = 0; i < stages.size(); ++i) {
            stages[i].A = stages[(i + 2) % 3].Y;
            stages[i].CONTROL = CONTROL;
            stages[i].CHARGE_BIAS = CHARGE_BIAS;
            stages[i].Vdd = Vdd;
            stages[i].Vss = Vss;
            stages[i].elaborate();
        }
    }
};
} // namespace analog
