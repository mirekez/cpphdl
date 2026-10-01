#pragma once
#include "cpphdl.h"

class WordGraph : public cpphdl::Module
{
public:
    _PORT(cpphdl::array<16, cpphdl::logic<64>, true>) payload_in;
    _PORT(cpphdl::logic<4>) select_in;
    _PORT(cpphdl::logic<1>) advance_in;
    _PORT(cpphdl::logic<64>) selected_out = _ASSIGN_COMB(selected_comb_func());
    _PORT(cpphdl::logic<64>) history_out = _ASSIGN_REG(history);

private:
    cpphdl::logic<64> selected_comb;
    cpphdl::reg<cpphdl::logic<64>> history;

    cpphdl::logic<64>& selected_comb_func()
    {
        selected_comb = 0;
        unsigned lane = 0;
        for (lane = 0; lane < 16; ++lane) {
            if (lane == uint64_t(select_in())) selected_comb = payload_in()[lane];
        }
        return selected_comb;
    }

public:
    void _assign() {}
    void _work(bool reset)
    {
        if (reset) history._next = 0;
        else if (advance_in()) history._next = selected_comb_func() ^ history;
        else history._next = history;
    }
    void _strobe() { history.strobe(); }
};
