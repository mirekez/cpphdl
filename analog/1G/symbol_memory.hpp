#pragma once
#include "parameters.hpp"
#include <basic/sim_clocked_storage.hpp>

namespace analog::one_g {
// Two nonoverlapping tracking phases retain an analog-valued previous symbol.
class sim_symbol_memory : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) CLOCK;
    _PORT(electrical) OUT;
    sim_sample_hold master, slave;

    void _assign() override {
        master.IN = IN;
        master.CLK = CLOCK;
        master.low = voltage(.25);
        master.high = voltage(.45);
        master.response = control_response::falling;
        slave.IN = master.OUT;
        slave.CLK = CLOCK;
        slave.OUT = OUT;
        slave.low = voltage(.75);
        slave.high = voltage(.95);
        slave.response = control_response::rising;
        for (auto* stage : {&master, &slave}) {
            stage->initial = voltage(common_mode);
            stage->storage = capacitance_value(100e-15);
            bind(*stage, Vdd, Vss);
        }
    }
};
} // namespace analog::one_g
