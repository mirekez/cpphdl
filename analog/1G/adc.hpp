#pragma once
#include <basic/mos_track_hold.hpp>
#include <basic/mos_r2r_dac.hpp>
#include <basic/strongarm_comparator.hpp>
#include <basic/nmos_resistor_amplifier.hpp>

namespace analog::one_g {

// Electrical half of a SAR converter. The controller changes MOS gate ports;
// the comparator's resolved outputs return its decision to the digital domain.
// Sources belong to the bench or connected reference/clock circuits.
class SARADCFrontend : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) TRACK;
    _PORT(electrical) EVALUATE;
    std::array<port<electrical>, 8> BIT_N;
    _PORT(electrical) POSITIVE;
    _PORT(electrical) NEGATIVE;
    mos_track_hold sampler;
    mos_r2r_dac ladder;
    nmos_resistor_amplifier preamplifier;
    strongarm_comparator comparator;

    void _assign() override {
        sampler.IN = IN;
        sampler.CLOCK = TRACK;
        sampler.Vdd = Vdd;
        sampler.Vss = Vss;
        sampler.elaborate();
        for (unsigned bit = 0; bit < 8; ++bit)
            ladder.BIT_N[bit] = BIT_N[bit];
        ladder.Vdd = Vdd;
        ladder.Vss = Vss;
        ladder.elaborate();
        // The latch's regeneration draws charge from its input capacitances.
        // Low-impedance drain loads absorb that kickback before it can corrupt
        // the much higher-impedance reference ladder or held input.
        preamplifier.PLUS = sampler.OUT;
        preamplifier.MINUS = ladder.OUT;
        preamplifier.gate_source = capacitance_value(.02e-12);
        preamplifier.gate_drain = capacitance_value(.003e-12);
        preamplifier.initial_minus = voltage(0);
        preamplifier.Vdd = Vdd;
        preamplifier.Vss = Vss;
        preamplifier.elaborate();
        comparator.PLUS = preamplifier.OUTP;
        comparator.MINUS = preamplifier.OUTN;
        comparator.CLOCK = EVALUATE;
        comparator.OUTP = POSITIVE;
        comparator.OUTN = NEGATIVE;
        comparator.initial_input = preamplifier.initial_supply;
        comparator.Vdd = Vdd;
        comparator.Vss = Vss;
        comparator.elaborate();
    }
};
} // namespace analog::one_g
