#pragma once
#include "channel.hpp"
#include "sfi_input.hpp"
#include "serializer.hpp"
#include "deserializer.hpp"
#include "tx_emphasis.hpp"
#include "ctle.hpp"
#include "pll_cdr.hpp"
#include "dfe.hpp"

namespace analog::ten_g {
class transmitter : public IC {
public:
    std::array<port<electrical>, 4> DATA;
    _PORT(electrical) CLOCK;
    _PORT(electrical) CLOCK_HALF;
    _PORT(electrical) CLOCK_QUARTER;
    _PORT(electrical) VCM;
    _PORT(electrical) P;
    _PORT(electrical) N;
    serializer serial;
    tap_history taps;
    tx_emphasis emphasis;

    void _assign() override {
        for (std::size_t i = 0; i < DATA.size(); ++i)
            serial.DATA[i] = DATA[i];
        serial.CLOCK_HALF = CLOCK_HALF;
        serial.CLOCK_QUARTER = CLOCK_QUARTER;
        serial.Vdd = Vdd;
        serial.Vss = Vss;
        serial.elaborate();
        taps.IN = serial.OUT;
        taps.CLOCK = CLOCK;
        taps.Vdd = Vdd;
        taps.Vss = Vss;
        taps.elaborate();
        emphasis.PRE = taps.history[0].Q;
        emphasis.MAIN = taps.history[1].Q;
        emphasis.POST = taps.history[2].Q;
        emphasis.VCM = VCM;
        emphasis.P = P;
        emphasis.N = N;
        emphasis.Vdd = Vdd;
        emphasis.Vss = Vss;
        emphasis.elaborate();
    }
};

class receiver : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) VCM;
    std::array<port<electrical>, 4> DATA;
    ctle equalizer;
    pll_cdr recovery;
    dfe feedback;
    deserializer parallel;

    void _assign() override {
        equalizer.P = P;
        equalizer.N = N;
        equalizer.VCM = VCM;
        equalizer.Vdd = Vdd;
        equalizer.Vss = Vss;
        equalizer.elaborate();
        recovery.P = equalizer.OUTP;
        recovery.N = equalizer.OUTN;
        recovery.VCM = VCM;
        recovery.Vdd = Vdd;
        recovery.Vss = Vss;
        recovery.elaborate();
        feedback.P = equalizer.OUTP;
        feedback.N = equalizer.OUTN;
        feedback.VCM = VCM;
        feedback.CLOCK = recovery.CLOCK;
        feedback.Vdd = Vdd;
        feedback.Vss = Vss;
        feedback.elaborate();
        parallel.IN = feedback.Q;
        parallel.CLOCK = recovery.CLOCK;
        parallel.Vdd = Vdd;
        parallel.Vss = Vss;
        for (std::size_t i = 0; i < DATA.size(); ++i)
            parallel.DATA[i] = DATA[i];
        parallel.elaborate();
    }
};

class link : public IC {
public:
    std::array<port<electrical>, 4> DATA;
    _PORT(electrical) CLOCK;
    _PORT(electrical) CLOCK_HALF;
    _PORT(electrical) CLOCK_QUARTER;
    _PORT(electrical) VCM;
    transmitter tx;
    channel medium;
    sfi_input interface;
    receiver rx;

    void _assign() override {
        for (std::size_t i = 0; i < DATA.size(); ++i)
            tx.DATA[i] = DATA[i];
        tx.CLOCK = CLOCK;
        tx.CLOCK_HALF = CLOCK_HALF;
        tx.CLOCK_QUARTER = CLOCK_QUARTER;
        tx.VCM = VCM;
        tx.Vdd = Vdd;
        tx.Vss = Vss;
        tx.elaborate();
        medium.P = tx.P;
        medium.N = tx.N;
        medium.VCM = VCM;
        medium.Vdd = Vdd;
        medium.Vss = Vss;
        // The SFI input owns the termination; the RC fixture adds no second load.
        medium.termination = resistance(1e9);
        medium.elaborate();
        interface.P = medium.OUTP;
        interface.N = medium.OUTN;
        interface.VCM = VCM;
        interface.Vdd = Vdd;
        interface.Vss = Vss;
        interface.elaborate();
        rx.P = interface.OUTP;
        rx.N = interface.OUTN;
        rx.VCM = VCM;
        rx.Vdd = Vdd;
        rx.Vss = Vss;
        rx.elaborate();
    }
};
} // namespace analog::ten_g
