#pragma once
#include "receive_frontend.hpp"
#include <basic/differential_source_follower.hpp>

namespace analog::one_g::ihp_sg13g2 {

// Accept the current-mode transmitter's higher MDI common mode. The input
// followers translate it before the existing amplifier and SAR bank.
template <std::size_t Count = 16> class mdi_receive_frontend : public IC {
public:
    _PORT(electrical) PLUS;
    _PORT(electrical) MINUS;
    _PORT(electrical) Vlogic;
    _PORT(electrical) EVALUATE;
    _PORT(electrical) CLEAR;
    std::array<port<electrical>, Count> TRACK, CORE_DECISION;
    std::array<std::array<port<electrical>, 8>, Count> BIT_N;
    differential_source_follower input;
    receive_frontend<Count> receiver;

    void _assign() override {
        input.PLUS = PLUS;
        input.MINUS = MINUS;
        input.OUTP = receiver.PLUS;
        input.OUTN = receiver.MINUS;
        input.Vdd = Vdd;
        input.Vss = Vss;
        input.elaborate();
        receiver.Vdd = Vdd;
        receiver.Vss = Vss;
        receiver.Vlogic = Vlogic;
        receiver.EVALUATE = EVALUATE;
        receiver.CLEAR = CLEAR;
        for (std::size_t i = 0; i < Count; ++i) {
            receiver.TRACK[i] = TRACK[i];
            receiver.CORE_DECISION[i] = CORE_DECISION[i];
            for (std::size_t bit = 0; bit < 8; ++bit)
                receiver.BIT_N[i][bit] = BIT_N[i][bit];
        }
        receiver.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
