#pragma once
#include "pam5.hpp"
#include <basic/sim_clocked_storage.hpp>

namespace analog::one_g {
// One fixed postcursor tap; feedback comes from achieved, held thermometer decisions.
class dfe : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) CLOCK;
    _PORT(electrical) VCM;
    _PORT(electrical) VREF;
    _PORT(electrical) VLOGIC;
    _PORT(electrical) OUT;
    electrical SUM;
    std::array<port<electrical>, 4> CODE;
    sim_pam5_slicer slicer;
    std::array<sim_dff, 4> decision;
    pam5_dac feedback;
    double tap = .35;

    void _assign() override {
        finite(tap, "DFE coefficient");
        if (std::abs(tap) >= .5)
            throw diagnostic("E_VALUE", "fixed prototype DFE requires |tap| < 0.5");
        SUM = resistor(VCM, resistance(1000));
        SUM = transconductor(VCM, IN, VCM, transconductance(.001));
        SUM = transconductor(VCM, VCM, OUT, transconductance(tap / 1000));
        slicer.IN = SUM;
        slicer.VCM = VCM;
        slicer.VLOGIC = VLOGIC;
        bind(slicer, Vdd, Vss);
        for (unsigned i = 0; i < CODE.size(); ++i) {
            decision[i].D = slicer.CODE[i];
            decision[i].CLK = CLOCK;
            decision[i].Q = CODE[i];
            decision[i].master.initial = voltage(i < 2 ? logic_supply : 0);
            decision[i].slave.initial = voltage(i < 2 ? logic_supply : 0);
            bind(decision[i], VLOGIC, Vss);
            feedback.CODE[i] = CODE[i];
        }
        feedback.VREF = VREF;
        feedback.VCM = VCM;
        feedback.OUT = OUT;
        feedback.step = symbol_step;
        bind(feedback, Vdd, Vss);
    }
};
} // namespace analog::one_g
