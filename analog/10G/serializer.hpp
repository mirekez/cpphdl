#pragma once
#include "parameters.hpp"
#include <basic/sim_clocked_storage.hpp>

namespace analog::ten_g {
// Quarter-rate input words; CLOCK_HALF and CLOCK_QUARTER are phase-related TX clock-tree outputs.
class serializer : public IC {
public:
    std::array<port<electrical>, 4> DATA;
    _PORT(electrical) CLOCK_HALF;
    _PORT(electrical) CLOCK_QUARTER;
    _PORT(electrical) OUT;
    sim_mux2 even, odd, output;

    void _assign() override {
        even.A = DATA[0];
        even.B = DATA[2];
        even.SELECT = CLOCK_QUARTER;
        odd.A = DATA[1];
        odd.B = DATA[3];
        odd.SELECT = CLOCK_QUARTER;
        output.A = even.Y;
        output.B = odd.Y;
        output.SELECT = CLOCK_HALF;
        output.Y = OUT;
        for (auto* mux : {&even, &odd, &output}) {
            mux->Vdd = Vdd;
            mux->Vss = Vss;
            mux->elaborate();
        }
    }
};

class tap_history : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) CLOCK;
    std::array<sim_dff, 3> history;

    void _assign() override {
        for (std::size_t i = 0; i < history.size(); ++i) {
            history[i].D = i == 0 ? electrical(IN) : electrical(history[i - 1].Q);
            history[i].CLK = CLOCK;
            history[i].Vdd = Vdd;
            history[i].Vss = Vss;
            history[i].elaborate();
        }
    }
};
} // namespace analog::ten_g
