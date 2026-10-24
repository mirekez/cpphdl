#pragma once
#include "wideband_receive_frontend.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Add another physical drive path across the same wires. Both paths contribute
// their channel current, bias return and stored charge to the shared circuit.
template <std::size_t Count = 16> class parallel_receive_frontend : public wideband_receive_frontend<Count> {
public:
    nmos_resistor_amplifier drive_amplifier;
    nmos_source_follower drive_buffer;

    void _assign() override {
        wideband_receive_frontend<Count>::_assign();
        drive_amplifier.PLUS = this->PLUS;
        drive_amplifier.MINUS = this->MINUS;
        drive_amplifier.OUTP = this->amplifier.OUTP;
        drive_amplifier.OUTN = this->amplifier.OUTN;
        drive_amplifier.load = resistance(500);
        drive_amplifier.output_storage = capacitance_value(100e-15);
        drive_amplifier.Vdd = this->Vdd;
        drive_amplifier.Vss = this->Vss;
        drive_amplifier.elaborate();
        drive_buffer.IN = this->amplifier.OUTP;
        drive_buffer.OUT = this->adc.IN;
        drive_buffer.Vdd = this->Vdd;
        drive_buffer.Vss = this->Vss;
        drive_buffer.channel = mos_model{channel_strength(.1), voltage(.7)};
        drive_buffer.bias_return = resistance(250);
        drive_buffer.gate_source = capacitance_value(40e-15);
        drive_buffer.gate_drain = capacitance_value(10e-15);
        drive_buffer.output_storage = capacitance_value(40e-15);
        drive_buffer.initial_input = this->amplifier.initial_supply;
        drive_buffer.initial_supply = this->amplifier.initial_supply;
        drive_buffer.initial_output = voltage(1.65);
        drive_buffer.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
