#pragma once

#include "cpphdl.h"

using namespace cpphdl;

template<size_t DEPTH = 16>
class SDFifo : public Module
{
public:
    static constexpr size_t INDEX_BITS = clog2(DEPTH);
    static constexpr size_t COUNT_BITS = clog2(DEPTH + 1);

    _PORT(bool) clear_in;
    _PORT(bool) push_in;
    _PORT(u<8>) push_data_in;
    _PORT(bool) pop_in;

    _PORT(bool) full_out = _ASSIGN(count_reg == DEPTH);
    _PORT(bool) empty_out = _ASSIGN(count_reg == 0);
    _PORT(bool) valid_out = _ASSIGN(count_reg != 0);
    _PORT(u<8>) data_out = _ASSIGN(data_reg[(uint32_t)rd_reg]);
    _PORT(u<COUNT_BITS>) count_out = _ASSIGN_REG(count_reg);

private:
    reg<array<u<8>, DEPTH>> data_reg;
    reg<u<INDEX_BITS>> rd_reg;
    reg<u<INDEX_BITS>> wr_reg;
    reg<u<COUNT_BITS>> count_reg;

    _LAZY_COMB(rd_next_comb, u<INDEX_BITS>)
        if ((uint32_t)rd_reg + 1u >= DEPTH) {
            return rd_next_comb = 0;
        }
        return rd_next_comb = rd_reg + 1;
    }

    _LAZY_COMB(wr_next_comb, u<INDEX_BITS>)
        if ((uint32_t)wr_reg + 1u >= DEPTH) {
            return wr_next_comb = 0;
        }
        return wr_next_comb = wr_reg + 1;
    }

public:
    void _work(bool reset)
    {
        bool do_push;
        bool do_pop;

        do_push = push_in() && count_reg != DEPTH;
        do_pop = pop_in() && count_reg != 0;

        if (do_push) {
            data_reg._next[(uint32_t)wr_reg] = push_data_in();
            wr_reg._next = wr_next_comb_func();
        }
        if (do_pop) {
            rd_reg._next = rd_next_comb_func();
        }

        if (do_push && !do_pop) {
            count_reg._next = count_reg + 1;
        }
        else if (!do_push && do_pop) {
            count_reg._next = count_reg - 1;
        }

        if (clear_in()) {
            rd_reg._next = 0;
            wr_reg._next = 0;
            count_reg._next = 0;
        }

        if (reset) {
            rd_reg.clr();
            wr_reg.clr();
            count_reg.clr();
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        data_reg.strobe(checkpoint_fd);
        rd_reg.strobe(checkpoint_fd);
        wr_reg.strobe(checkpoint_fd);
        count_reg.strobe(checkpoint_fd);
    }
};
