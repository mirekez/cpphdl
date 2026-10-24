#pragma once
#include "inverter.hpp"

namespace analog {

// A bidirectional CMOS transmission gate charges a real hold capacitor.
// CLOCK high tracks IN; CLOCK low opens both channels. Loading, leakage and
// gate/input coupling all remain in the shared electrical equation system.
class mos_track_hold : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) CLOCK;
    _PORT(electrical) OUT;
    cmos<channel_model> complement;
    mos_model n_channel{channel_strength(.02), voltage(.7)};
    mos_model p_channel{channel_strength(.01), voltage(.7)};
    mos_model dummy_n{channel_strength(.01), voltage(.7)};
    mos_model dummy_p{channel_strength(.005), voltage(.7)};
    capacitance_value storage{2e-12}, clock_storage{5e-15};
    capacitance_value gate_coupling{2e-15}, off_coupling{1e-15};
    resistance leakage{1e9};
    bool leakage_return{true};
    voltage initial_input{1.65}, initial_supply{3.3};

    void _assign() override {
        complement.A = CLOCK;
        complement.model.transistor_parameters = mos_model{channel_strength(.004), voltage(.7)};
        complement.Vdd = Vdd;
        complement.Vss = Vss;
        complement.elaborate();
        complement.Y = capacitor(Vss, clock_storage, initial_supply);
        OUT = nmos(CLOCK, IN, n_channel);
        OUT = pmos(complement.Y, IN, p_channel);
        // Half-width dummy channels take opposing charge when the main switch
        // opens. Their intrinsic charge is present in process compact models;
        // the generic channel law models only conduction and explicit capacitors.
        OUT = nmos(complement.Y, OUT, dummy_n);
        OUT = pmos(CLOCK, OUT, dummy_p);
        OUT = capacitor(Vss, storage, initial_input);
        if (leakage_return)
            OUT = resistor(Vss, leakage);
        CLOCK = capacitor(OUT, gate_coupling, voltage(-initial_input.value));
        complement.Y = capacitor(OUT, gate_coupling, voltage(initial_supply.value - initial_input.value));
        IN = capacitor(OUT, off_coupling); // Input changes can still disturb a held voltage.
    }
};
} // namespace analog
