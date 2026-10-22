#pragma once
#include "nmos_source_follower.hpp"
#include <array>

namespace analog {

// Lower the input common mode while preserving differential polarity. Each
// source follows its gate through a physical NMOS and resistive bias return.
class differential_source_follower : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) OUTP;
    _PORT(electrical) OUTN;
    std::array<nmos_source_follower, 2> followers;
    voltage initial_input{2.8}, initial_output{1.65}, initial_supply{3.3};

    void _assign() override {
        followers[0].IN = PLUS;
        followers[1].IN = MINUS;
        followers[0].OUT = OUTP;
        followers[1].OUT = OUTN;
        for (auto& follower : followers) {
            follower.Vdd = Vdd;
            follower.Vss = Vss;
            follower.initial_input = initial_input;
            follower.initial_output = initial_output;
            follower.initial_supply = initial_supply;
            follower.elaborate();
        }
    }
};
} // namespace analog
