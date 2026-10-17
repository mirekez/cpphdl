#pragma once
#include "cmos_logic.hpp"
#include "ring_vco.hpp"

namespace analog {
class lead_lag_filter : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    electrical RETURN;
    resistance series{10000}, damping{470};
    capacitance_value storage{100e-9};
    voltage initial{.6};

    void _assign() override {
        OUT = resistor(IN, series);
        OUT = capacitor(RETURN, storage, initial);
        Vss = resistor(RETURN, damping); // The resistor adds a zero to the loop filter response.
    }
};

// XOR phase detector + passive lead/lag filter + three physical inverter stages.
class pll : public IC {
public:
    _PORT(electrical) REF;
    _PORT(electrical) OUT;
    electrical CONTROL;
    cmos_xor detector;
    lead_lag_filter filter;
    ring_vco oscillator;

    void _assign() override {
        detector.A = REF;
        detector.B = OUT;
        detector.Vdd = Vdd;
        detector.Vss = Vss;
        detector.elaborate();
        filter.IN = detector.Y;
        filter.OUT = CONTROL;
        filter.Vdd = Vdd;
        filter.Vss = Vss;
        filter.elaborate();
        oscillator.CONTROL = CONTROL;
        oscillator.OUT = OUT;
        oscillator.Vdd = Vdd;
        oscillator.Vss = Vss;
        oscillator.elaborate();
    }
};
} // namespace analog
