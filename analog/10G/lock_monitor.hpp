#pragma once
#include "parameters.hpp"
#include <basic/bridge_rectifier.hpp>
#include <basic/gm_stage.hpp>
#include <basic/sim_clocked_storage.hpp>

namespace analog::ten_g {
// Local PLL qualification: sustained low filtered pulse imbalance AND sufficient transition activity.
// This does not replace wide-range frequency acquisition or PCS block lock.
class lock_monitor : public IC {
public:
    _PORT(electrical) ERROR;
    _PORT(electrical) REFERENCE;
    _PORT(electrical) VCM;
    _PORT(electrical) LOCK;
    electrical IMBALANCE, ACTIVITY, ENVELOPE, THRESHOLD, RAW;
    bridge_rectifier magnitude;
    limiting_amplifier phase_good, activity_good;
    sim_mux2 both;

    void _assign() override {
        IMBALANCE = resistor(Vss, resistance(1000));
        IMBALANCE = transconductor(Vss, ERROR, REFERENCE, transconductance(.001));
        IMBALANCE = capacitor(Vss, capacitance_value(8 * ui / 1000));
        ACTIVITY = resistor(REFERENCE, resistance(1000));
        ACTIVITY = capacitor(Vss, capacitance_value(4 * ui / 1000));
        magnitude.PLUS = IMBALANCE;
        magnitude.MINUS = Vss;
        magnitude.Vdd = Vdd;
        magnitude.Vss = Vss;
        magnitude.elaborate();
        ENVELOPE = resistor(Vss, resistance(1000));
        ENVELOPE = transconductor(Vss, magnitude.POS, magnitude.NEG, transconductance(.001));
        ENVELOPE = capacitor(Vss, capacitance_value(4 * ui / 1000));
        THRESHOLD = resistor(VCM, resistance(100000));
        Vss = resistor(THRESHOLD, resistance(10000));
        phase_good.PLUS = THRESHOLD;
        phase_good.MINUS = ENVELOPE;
        activity_good.PLUS = ACTIVITY;
        activity_good.MINUS = THRESHOLD;
        for (auto* comparator : {&phase_good, &activity_good}) {
            comparator->BIAS = VCM;
            comparator->gain = transconductance(.1);
            comparator->clamp_resistance = resistance(.1);
            comparator->Vdd = Vdd;
            comparator->Vss = Vss;
            comparator->elaborate();
        }
        both.A = Vss;
        both.B = phase_good.OUT;
        both.SELECT = activity_good.OUT;
        both.Y = RAW;
        both.Vdd = Vdd;
        both.Vss = Vss;
        both.elaborate();
        LOCK = resistor(RAW, resistance(1000));
        LOCK = capacitor(Vss, capacitance_value(4 * ui / 1000));
    }
};
} // namespace analog::ten_g
