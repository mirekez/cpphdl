#pragma once
#include "parameters.hpp"

namespace analog::one_g {
// Reciprocal lumped fixture: either endpoint can drive and load the same wires.
// This is a short-channel development fixture, not a 100 m Cat5 cable model.
class cable_pair : public IC {
public:
    _PORT(electrical) AP;
    _PORT(electrical) AN;
    _PORT(electrical) BP;
    _PORT(electrical) BN;
    _PORT(electrical) VCM;
    resistance series{15}; // Per conductor, end to end.
    capacitance_value shunt{5e-12}; // Per conductor at each end.

    void _assign() override {
        BP = resistor(AP, series);
        BN = resistor(AN, series);
        for (auto* wire : {&AP, &AN, &BP, &BN})
            *wire = capacitor(VCM, shunt);
    }
};

class four_pair_cable : public IC {
public:
    std::array<port<electrical>, pairs> AP, AN, BP, BN;
    _PORT(electrical) VCM;
    std::array<cable_pair, pairs> lane;
    resistance coupling{5000}; // Symmetric lossy coupling fixture, not a NEXT mask.

    void _assign() override {
        for (unsigned i = 0; i < pairs; ++i) {
            lane[i].AP = AP[i];
            lane[i].AN = AN[i];
            lane[i].BP = BP[i];
            lane[i].BN = BN[i];
            lane[i].VCM = VCM;
            bind(lane[i], Vdd, Vss);
            unsigned next = (i + 1) % pairs;
            AP[i] = resistor(AP[next], coupling);
            AN[i] = resistor(AN[next], coupling);
            BP[i] = resistor(BP[next], coupling);
            BN[i] = resistor(BN[next], coupling);
        }
    }
};
} // namespace analog::one_g
