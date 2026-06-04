#pragma once

#include "cpphdl.h"
#include "ethgig_pcs.h"

using namespace cpphdl;

template<size_t FIFO_DEPTH = 2048>
class EthGigMAC : public Module
{
public:
    _PORT(bool) tx_valid_in;
    _PORT(u<8>) tx_data_in;
    _PORT(bool) tx_last_in;
    _PORT(bool) tx_ready_out = _ASSIGN(tx_count_reg != FIFO_DEPTH);

    _PORT(bool) rx_valid_out = _ASSIGN(rx_count_reg != 0);
    _PORT(u<8>) rx_data_out = _ASSIGN(rx_data_reg[(uint32_t)rx_rd_reg]);
    _PORT(bool) rx_last_out = _ASSIGN(rx_last_reg[(uint32_t)rx_rd_reg]);
    _PORT(bool) rx_ready_in;

    _PORT(bool) pcs_tx_valid_out = _ASSIGN(tx_count_reg != 0);
    _PORT(u<8>) pcs_tx_data_out = _ASSIGN(tx_data_reg[(uint32_t)tx_rd_reg]);
    _PORT(bool) pcs_tx_last_out = _ASSIGN(tx_last_reg[(uint32_t)tx_rd_reg]);
    _PORT(bool) pcs_tx_ready_in;

    _PORT(bool) pcs_rx_valid_in;
    _PORT(u<8>) pcs_rx_data_in;
    _PORT(bool) pcs_rx_last_in;
    _PORT(bool) pcs_rx_ready_out = _ASSIGN(rx_count_reg != FIFO_DEPTH);

    _PORT(uint32_t) tx_frames_out = _ASSIGN_REG(tx_frames_reg);
    _PORT(uint32_t) rx_frames_out = _ASSIGN_REG(rx_frames_reg);
    _PORT(uint32_t) tx_bytes_out = _ASSIGN_REG(tx_bytes_reg);
    _PORT(uint32_t) rx_bytes_out = _ASSIGN_REG(rx_bytes_reg);

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

    reg<u32> tx_frames_reg;
    reg<u32> rx_frames_reg;
    reg<u32> tx_bytes_reg;
    reg<u32> rx_bytes_reg;

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
        tx_pop = pcs_tx_valid_out() && pcs_tx_ready_in();
        rx_push = pcs_rx_valid_in() && pcs_rx_ready_out();
        rx_pop = rx_valid_out() && rx_ready_in();

        if (tx_push) {
            tx_data_reg._next[(uint32_t)tx_wr_reg] = tx_data_in();
            tx_last_reg._next[(uint32_t)tx_wr_reg] = tx_last_in();
            tx_wr_reg._next = tx_wr_next_comb_func();
        }
        if (tx_pop) {
            tx_rd_reg._next = tx_rd_next_comb_func();
            tx_bytes_reg._next = tx_bytes_reg + 1;
            if (pcs_tx_last_out()) {
                tx_frames_reg._next = tx_frames_reg + 1;
            }
        }
        if (tx_push && !tx_pop) {
            tx_count_reg._next = tx_count_reg + 1;
        }
        else if (!tx_push && tx_pop) {
            tx_count_reg._next = tx_count_reg - 1;
        }

        if (rx_push) {
            rx_data_reg._next[(uint32_t)rx_wr_reg] = pcs_rx_data_in();
            rx_last_reg._next[(uint32_t)rx_wr_reg] = pcs_rx_last_in();
            rx_wr_reg._next = rx_wr_next_comb_func();
            rx_bytes_reg._next = rx_bytes_reg + 1;
            if (pcs_rx_last_in()) {
                rx_frames_reg._next = rx_frames_reg + 1;
            }
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
            tx_frames_reg._next = 0;
            rx_frames_reg._next = 0;
            tx_bytes_reg._next = 0;
            rx_bytes_reg._next = 0;
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
        tx_frames_reg.strobe();
        rx_frames_reg.strobe();
        tx_bytes_reg.strobe();
        rx_bytes_reg.strobe();
    }
};
