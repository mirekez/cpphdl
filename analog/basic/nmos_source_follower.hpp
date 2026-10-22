#pragma once
#include <analog.hpp>

namespace analog {

// A source follower lowers the driving impedance at the cost of Vgs headroom
// and continuous bias current. The return resistor also limits discharge of
// a connected sampling capacitor; output load remains in the same equations.
class nmos_source_follower : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    mos_model channel{channel_strength(.05), voltage(.7)};
    resistance bias_return{500};
    capacitance_value gate_source{20e-15}, gate_drain{5e-15}, output_storage{20e-15};
    voltage initial_input{0}, initial_output{0}, initial_supply{0};

    void _assign() override {
        OUT = nmos(IN, Vdd, channel);
        OUT = resistor(Vss, bias_return);
        // These are initial charges. A connected amplifier/ADC may start
        // precharged; its shared-node voltages must describe one consistent state.
        IN = capacitor(OUT, gate_source, voltage(initial_input.value - initial_output.value));
        IN = capacitor(Vdd, gate_drain, voltage(initial_input.value - initial_supply.value));
        OUT = capacitor(Vss, output_storage, initial_output);
    }
};
} // namespace analog
