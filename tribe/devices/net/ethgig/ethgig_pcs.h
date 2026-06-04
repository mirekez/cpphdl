#pragma once

#include "cpphdl.h"

using namespace cpphdl;

template<size_t FIFO_DEPTH = 64>
class EthGigPCS : public Module
{
public:
    _PORT(bool) tx_valid_in;
    _PORT(u<8>) tx_data_in;
    _PORT(bool) tx_last_in;
    _PORT(bool) tx_ready_out = _ASSIGN(tx_count_reg != FIFO_DEPTH);

    _PORT(bool) tx_valid_out = _ASSIGN(tx_count_reg != 0);
    _PORT(u<8>) tx_data_out = _ASSIGN(tx_data_reg[(uint32_t)tx_rd_reg]);
    _PORT(bool) tx_last_out = _ASSIGN(tx_last_reg[(uint32_t)tx_rd_reg]);
    _PORT(bool) tx_ready_in;

    _PORT(bool) rx_valid_in;
    _PORT(u<8>) rx_data_in;
    _PORT(bool) rx_last_in;
    _PORT(bool) rx_ready_out = _ASSIGN(rx_count_reg != FIFO_DEPTH);

    _PORT(bool) rx_valid_out = _ASSIGN(rx_count_reg != 0);
    _PORT(u<8>) rx_data_out = _ASSIGN(rx_data_reg[(uint32_t)rx_rd_reg]);
    _PORT(bool) rx_last_out = _ASSIGN(rx_last_reg[(uint32_t)rx_rd_reg]);
    _PORT(bool) rx_ready_in;

private:
    static constexpr size_t INDEX_BITS = clog2(FIFO_DEPTH);
    static constexpr size_t COUNT_BITS = clog2(FIFO_DEPTH + 1);

    reg<array<u<8>, FIFO_DEPTH>> tx_data_reg;
    reg<array<u1, FIFO_DEPTH>> tx_last_reg;
    reg<u<INDEX_BITS>> tx_rd_reg;
    reg<u<INDEX_BITS>> tx_wr_reg;
    reg<u<COUNT_BITS>> tx_count_reg;

    reg<array<u<8>, FIFO_DEPTH>> rx_data_reg;
    reg<array<u1, FIFO_DEPTH>> rx_last_reg;
    reg<u<INDEX_BITS>> rx_rd_reg;
    reg<u<INDEX_BITS>> rx_wr_reg;
    reg<u<COUNT_BITS>> rx_count_reg;

    _LAZY_COMB(tx_rd_next_comb, u<INDEX_BITS>)
        if ((uint32_t)tx_rd_reg + 1u >= FIFO_DEPTH) {
            return tx_rd_next_comb = 0;
        }
        return tx_rd_next_comb = tx_rd_reg + 1;
    }

    _LAZY_COMB(tx_wr_next_comb, u<INDEX_BITS>)
        if ((uint32_t)tx_wr_reg + 1u >= FIFO_DEPTH) {
            return tx_wr_next_comb = 0;
        }
        return tx_wr_next_comb = tx_wr_reg + 1;
    }

    _LAZY_COMB(rx_rd_next_comb, u<INDEX_BITS>)
        if ((uint32_t)rx_rd_reg + 1u >= FIFO_DEPTH) {
            return rx_rd_next_comb = 0;
        }
        return rx_rd_next_comb = rx_rd_reg + 1;
    }

    _LAZY_COMB(rx_wr_next_comb, u<INDEX_BITS>)
        if ((uint32_t)rx_wr_reg + 1u >= FIFO_DEPTH) {
            return rx_wr_next_comb = 0;
        }
        return rx_wr_next_comb = rx_wr_reg + 1;
    }

public:
    void _assign() {}

    void _work(bool reset)
    {
        bool tx_push;
        bool tx_pop;
        bool rx_push;
        bool rx_pop;

        tx_push = tx_valid_in() && tx_ready_out();
        tx_pop = tx_valid_out() && tx_ready_in();
        rx_push = rx_valid_in() && rx_ready_out();
        rx_pop = rx_valid_out() && rx_ready_in();

        if (tx_push) {
            tx_data_reg._next[(uint32_t)tx_wr_reg] = tx_data_in();
            tx_last_reg._next[(uint32_t)tx_wr_reg] = tx_last_in();
            tx_wr_reg._next = tx_wr_next_comb_func();
        }
        if (tx_pop) {
            tx_rd_reg._next = tx_rd_next_comb_func();
        }
        if (tx_push && !tx_pop) {
            tx_count_reg._next = tx_count_reg + 1;
        }
        else if (!tx_push && tx_pop) {
            tx_count_reg._next = tx_count_reg - 1;
        }

        if (rx_push) {
            rx_data_reg._next[(uint32_t)rx_wr_reg] = rx_data_in();
            rx_last_reg._next[(uint32_t)rx_wr_reg] = rx_last_in();
            rx_wr_reg._next = rx_wr_next_comb_func();
        }
        if (rx_pop) {
            rx_rd_reg._next = rx_rd_next_comb_func();
        }
        if (rx_push && !rx_pop) {
            rx_count_reg._next = rx_count_reg + 1;
        }
        else if (!rx_push && rx_pop) {
            rx_count_reg._next = rx_count_reg - 1;
        }

        if (reset) {
            tx_rd_reg._next = 0;
            tx_wr_reg._next = 0;
            tx_count_reg._next = 0;
            rx_rd_reg._next = 0;
            rx_wr_reg._next = 0;
            rx_count_reg._next = 0;
        }
    }

    void _strobe()
    {
        tx_data_reg.strobe();
        tx_last_reg.strobe();
        tx_rd_reg.strobe();
        tx_wr_reg.strobe();
        tx_count_reg.strobe();
        rx_data_reg.strobe();
        rx_last_reg.strobe();
        rx_rd_reg.strobe();
        rx_wr_reg.strobe();
        rx_count_reg.strobe();
    }
};
