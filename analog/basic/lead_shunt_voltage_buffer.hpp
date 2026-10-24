#pragma once
#include "shunt_voltage_buffer.hpp"

namespace analog {

// The series RC branch compensates the shunt feedback loop. The sink size
// retains OTA output headroom while storage absorbs simultaneous load steps.
class lead_shunt_voltage_buffer : public shunt_voltage_buffer {
public:
    void _assign() override {
        electrical lead;
        sink_channel = mos_model{channel_strength(.03), voltage(.7)};
        storage = capacitance_value(80e-12);
        shunt_voltage_buffer::_assign();
        lead = resistor(DRIVE, resistance(1000));
        lead = capacitor(OUT, capacitance_value(2e-12));
    }
};

} // namespace analog
