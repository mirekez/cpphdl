#pragma once
#include <analog.hpp>

namespace analog {
class bridge_rectifier : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) POS;
    _PORT(electrical) NEG;
    resistance forward{10}, load{10000}, leakage_return{1e9};

    void _assign() override {
        PLUS = diode(POS, forward);
        MINUS = diode(POS, forward);
        NEG = diode(PLUS, forward);
        NEG = diode(MINUS, forward);
        NEG = resistor(POS, load);
        Vss = resistor(NEG, leakage_return); // Fixes floating common mode through an explicit leakage path.
    }
};
} // namespace analog
