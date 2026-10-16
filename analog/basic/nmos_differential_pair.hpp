#pragma once
#include <analog.hpp>

namespace analog {

class nmos_differential_pair : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) LEFT;
    _PORT(electrical) RIGHT;
    _PORT(electrical) TAIL;
    mos_model model{channel_strength(.2), analog::voltage(.35)};

    void _assign() override {
        TAIL = nmos(PLUS, LEFT, model); // PLUS steers current through the left branch.
        TAIL = nmos(MINUS, RIGHT, model); // Both branches share their source connection.
    }
};

} // namespace analog
