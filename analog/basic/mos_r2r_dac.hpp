#pragma once
#include "inverter.hpp"

namespace analog {

// Eight CMOS rail drivers feed a passive R/2R ladder. BIT_N[0] is the
// complemented least-significant bit. The output has approximately R source
// resistance; the next circuit's loading changes its achieved voltage.
class mos_r2r_dac : public IC {
public:
    std::array<port<electrical>, 8> BIT_N;
    _PORT(electrical) OUT;
    std::array<cmos<channel_model>, 8> driver;
    std::array<electrical, 8> tap;
    resistance ladder{10000};
    mos_model rail_channel{channel_strength(.08), voltage(.7)};
    capacitance_value parasitic{3e-15};

    void _assign() override {
        tap[7] = OUT;
        for (unsigned bit = 0; bit < 8; ++bit) {
            driver[bit].A = BIT_N[bit];
            driver[bit].Vdd = Vdd;
            driver[bit].Vss = Vss;
            driver[bit].model.transistor_parameters = rail_channel;
            driver[bit].elaborate();
            driver[bit].Y = capacitor(Vss, parasitic);
            tap[bit] = resistor(driver[bit].Y, resistance(2 * ladder.value));
            tap[bit] = capacitor(Vss, parasitic);
            if (bit)
                tap[bit] = resistor(tap[bit - 1], ladder);
        }
        tap[0] = resistor(Vss, resistance(2 * ladder.value));
    }
};
} // namespace analog
