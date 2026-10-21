#pragma once
#include <analog.hpp>

namespace analog {

class rc_filter : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    resistance series{1000};
    capacitance_value storage{1e-9};

    void _assign() override {
        OUT = resistor(IN, series); // Deliver the input with finite drive.
        OUT = capacitor(Vss, storage); // Charge storage opposes fast output changes.
    }
};

} // namespace analog
