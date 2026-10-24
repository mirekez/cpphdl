#pragma once
#include "parameters.hpp"

namespace analog::one_g {

// Preserve more of the differential line signal while an external physical
// common-mode source absorbs the injected DC current. The receiver supplies
// its own 100-ohm input termination. VCM loading couples every connected lane.
class biased_mdi_input_network : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) VCM;
    resistance series{500}, common_return{150};

    void _assign() override {
        PLUS = resistor(P, series);
        MINUS = resistor(N, series);
        PLUS = resistor(VCM, common_return);
        MINUS = resistor(VCM, common_return);
    }
};

} // namespace analog::one_g
