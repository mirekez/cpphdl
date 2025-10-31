#pragma once

#include "cpphdl.h"
#include "Memory.h"

template<size_t FIFO_WIDTH_BYTES, size_t FIFO_DEPTH, bool SHOWAHEAD = true>
class Fifo : public Module
{
    Memory<FIFO_WIDTH_BYTES,FIFO_DEPTH,SHOWAHEAD> mem;

    u1 full_comb;
    u1 empty_comb;

    reg<u<clog2(FIFO_DEPTH)>> wp_reg;
    reg<u<clog2(FIFO_DEPTH)>> rp_reg;
    reg<u1> full_reg;

    reg<u1> afull_reg;


public:
    bool*                        write_in  = nullptr;
    logic<FIFO_WIDTH_BYTES*8>*   data_in   = nullptr;

    bool*                        read_in   = nullptr;
    logic<FIFO_WIDTH_BYTES*8>*   data_out  = mem.data_out;

    bool*                        empty_out = &empty_comb;
    bool*                        full_out  = &full_comb;
    bool*                        clear_in  = &ZERO;
    bool*                        afull_out = &afull_reg;

    void connect()
    {
        mem.data_in       = data_in;
        mem.write_in      = write_in;
        mem.write_addr_in = &wp_reg;
        mem.read_in       = read_in;
        mem.read_addr_in  = &rp_reg;
        mem.connect();
    }

    bool full_comb_func()
    {
        full_comb = (wp_reg.next == rp_reg.next) && full_reg.next;
        return full_comb;
    }

    bool empty_comb_func()
    {
        empty_comb = (wp_reg.next == rp_reg.next) && !full_reg.next;
        return empty_comb;
    }

    void work(bool clk_in, bool reset_in)
    {
        if (!clk_in) return;
        mem.work(clk_in, reset_in);

        if (reset_in) {
            wp_reg.clr();
            rp_reg.clr();
            full_reg.clr();
            afull_reg.clr();
            return;
        }

        if (*read_in) {

            if (empty_comb_func()) {
                printf("%s: reading from an empty fifo\n", name.c_str());
                exit(1);
            }
            if (!empty_comb_func()) {
                rp_reg.next = rp_reg + 1;
            }
            if (!*write_in) {
                full_reg.next = 0;
            }
        }

        if (*write_in) {

            if (full_comb_func()) {
                printf("%s: writing to a full fifo\n", name.c_str());
                exit(1);
            }
            if (!full_comb_func()) {
                wp_reg.next = wp_reg + 1;
            }
            if (wp_reg.next == rp_reg.next) {
                full_reg.next = 1;
            }
        }

        if (*clear_in) {
            wp_reg.next = 0;
            rp_reg.next = 0;
            full_reg.next = 0;
        }

        afull_reg.next = full_reg || (wp_reg >= rp_reg ? wp_reg - rp_reg : FIFO_DEPTH - rp_reg + wp_reg) >= FIFO_DEPTH/2;
    }

    void strobe()
    {
        wp_reg.strobe();
        rp_reg.strobe();
        full_reg.strobe();
        afull_reg.strobe();
        mem.strobe();
    }

    void comb()
    {
        mem.comb();
        mem.data_out_comb_func();
    }
};

template struct Fifo<32,1024>;

#ifndef SYNTHESIS

struct FifoTest
{
};

#endif
