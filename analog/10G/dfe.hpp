#pragma once
#include "parameters.hpp"
#include <basic/gm_stage.hpp>
#include <basic/sim_clocked_storage.hpp>

namespace analog::ten_g {
// One postcursor tap. Feedback is the achieved previous decision, not a testbench oracle.
class dfe : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) VCM;
    _PORT(electrical) CLOCK;
    _PORT(electrical) Q;
    electrical SUM;
    double tap = .025; // Differential volts canceled per previous +/-1 decision.
    limiting_amplifier slicer;
    sim_dff decision;

    void _assign() override {
        finite(tap, "DFE tap");
        if (std::abs(tap) > .2)
            throw diagnostic("E_VALUE", "DFE tap exceeds documented input range");
        SUM = resistor(VCM, resistance(1000));
        SUM = transconductor(VCM, P, N, transconductance(.001));
        SUM = transconductor(VCM, Q, VCM, transconductance(-tap / (common_mode * 1000)));
        slicer.PLUS = SUM;
        slicer.MINUS = VCM;
        slicer.BIAS = VCM;
        slicer.Vdd = Vdd;
        slicer.Vss = Vss;
        slicer.gain = transconductance(.1);
        slicer.clamp_resistance = resistance(.1);
        slicer.elaborate();
        decision.D = slicer.OUT;
        decision.CLK = CLOCK;
        decision.Q = Q;
        decision.Vdd = Vdd;
        decision.Vss = Vss;
        decision.elaborate();
    }
};
} // namespace analog::ten_g
