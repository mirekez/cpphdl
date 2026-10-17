#pragma once
#include "pam5.hpp"
#include "symbol_memory.hpp"

namespace analog::one_g {
// The 3/4 + 1/4 z^-1 response has seventeen possible levels, despite PAM5 input.
class pulse_shaper : public IC {
public:
    _PORT(electrical) MAIN;
    _PORT(electrical) HISTORY;
    _PORT(electrical) VCM;
    _PORT(electrical) OUT;
    double main = .75, history = .25;

    void _assign() override {
        finite(main, "main coefficient");
        finite(history, "history coefficient");
        OUT = resistor(VCM, resistance(1000));
        OUT = capacitor(VCM, capacitance_value(20e-15));
        OUT = transconductor(VCM, MAIN, VCM, transconductance(main / 1000));
        OUT = transconductor(VCM, HISTORY, VCM, transconductance(history / 1000));
    }
};

class line_driver : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) VCM;
    _PORT(electrical) P;
    _PORT(electrical) N;
    resistance source{50}; // Each leg; 100 Ohm differential source impedance.

    void _assign() override {
        P = resistor(VCM, source);
        N = resistor(VCM, source);
        P = transconductor(VCM, IN, VCM, transconductance(.5 / source.value));
        N = transconductor(VCM, VCM, IN, transconductance(.5 / source.value));
    }
};

class transmitter : public IC {
public:
    std::array<port<electrical>, 4> CODE;
    _PORT(electrical) CLOCK;
    _PORT(electrical) VREF;
    _PORT(electrical) VCM;
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) REPLICA;
    pam5_dac dac;
    sim_symbol_memory delay;
    pulse_shaper filter;
    line_driver driver;

    void _assign() override {
        for (unsigned i = 0; i < CODE.size(); ++i)
            dac.CODE[i] = CODE[i];
        dac.VREF = VREF;
        dac.VCM = VCM;
        bind(dac, Vdd, Vss);
        delay.IN = dac.OUT;
        delay.CLOCK = CLOCK;
        bind(delay, Vdd, Vss);
        filter.MAIN = dac.OUT;
        filter.HISTORY = delay.OUT;
        filter.VCM = VCM;
        filter.OUT = REPLICA;
        bind(filter, Vdd, Vss);
        driver.IN = REPLICA;
        driver.VCM = VCM;
        driver.P = P;
        driver.N = N;
        bind(driver, Vdd, Vss);
    }
};
} // namespace analog::one_g
