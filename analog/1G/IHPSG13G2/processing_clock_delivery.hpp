#pragma once
#include "processing_clock_oscillator.hpp"
#include "processing_clock_gate.hpp"

namespace analog::one_g::ihp_sg13g2 {

// The calibration controller observes RAW even while the receiver is stopped.
// Its synchronized readiness drives ADMIT; the low-phase latch admits complete
// pulses to OUT. Supply and transistor loading are shared physical nodes.
class processing_clock_delivery : public IC {
public:
    std::array<port<electrical>, 8> COARSE_N, FINE_N;
    _PORT(electrical) CLEAR;
    _PORT(electrical) ADMIT;
    _PORT(electrical) RAW;
    _PORT(electrical) OUT;
    processing_clock_oscillator oscillator;
    processing_clock_gate admission;

    void _assign() override {
        oscillator.Vdd = Vdd;
        oscillator.Vss = Vss;
        oscillator.CLEAR = CLEAR;
        oscillator.OUT = RAW;
        for (unsigned bit = 0; bit < 8; ++bit) {
            oscillator.COARSE_N[bit] = COARSE_N[bit];
            oscillator.FINE_N[bit] = FINE_N[bit];
        }
        oscillator.elaborate();
        admission.Vdd = Vdd;
        admission.Vss = Vss;
        admission.CLOCK = RAW;
        admission.ENABLE = ADMIT;
        admission.OUT = OUT;
        admission.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
