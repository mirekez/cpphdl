#pragma once
#include <analog.hpp>

namespace analog {

class resistor_divider : public IC {
public:
    _PORT(electrical) TOP;
    _PORT(electrical) BOTTOM;
    _PORT(electrical) TAP;
    resistance upper{10000};
    resistance lower{10000};

    void _assign() override {
        TAP = resistor(TOP, upper); // Deliver the top potential through a finite resistance.
        BOTTOM = resistor(TAP, lower); // Load the tap toward the lower terminal.
    }
};

} // namespace analog
