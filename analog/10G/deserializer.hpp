#pragma once
#include "parameters.hpp"
#include <basic/sim_clocked_storage.hpp>

namespace analog::ten_g {
// Sliding four-symbol window. A host word clock/block aligner selects every fourth window.
class deserializer : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) CLOCK;
    std::array<port<electrical>, 4> DATA;
    std::array<sim_dff, 4> history;

    void _assign() override {
        for (std::size_t i = 0; i < history.size(); ++i) {
            history[i].D = i == 0 ? electrical(IN) : electrical(history[i - 1].Q);
            history[i].CLK = CLOCK;
            history[i].Q = DATA[i];
            history[i].Vdd = Vdd;
            history[i].Vss = Vss;
            history[i].elaborate();
        }
    }
};
} // namespace analog::ten_g
