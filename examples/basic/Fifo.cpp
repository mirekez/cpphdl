#pragma once
#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "Memory.cpp"

using namespace cpphdl;

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

        mem.inst_name = inst_name + "/mem";
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

        DEBUG("{}: input: ({}){}, output: ({}){}, full: {}, empty: {}\n", inst_name, (int)*write_in, *data_in, (int)*read_in, *data_out, (int)*full_out, (int)*empty_out);

        if (reset_in) {
            wp_reg.clr();
            rp_reg.clr();
            full_reg.clr();
            afull_reg.clr();
            return;
        }

        if (*read_in) {

            if (empty_comb_func()) {
                printf("%s: reading from an empty fifo\n", inst_name.c_str());
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
                printf("%s: writing to a full fifo\n", inst_name.c_str());
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

template<size_t FIFO_WIDTH_BYTES, size_t FIFO_DEPTH, bool SHOWAHEAD>
class TestFifo : public Module
{
    Fifo<FIFO_WIDTH_BYTES,FIFO_DEPTH,SHOWAHEAD> fifo;

    reg<u<clog2(FIFO_DEPTH)>>       read_addr;
    reg<u<clog2(FIFO_DEPTH)>>       write_addr;
    reg<logic<FIFO_WIDTH_BYTES*8>>  data_reg;
    reg<u1>                         write_reg;
    reg<u1>                         read_reg;
    reg<u1>  was_read;
    reg<u1>  clear_reg;
    reg<u16> to_write_cnt;
    reg<u16> to_read_cnt;
    bool     error = false;

    std::array<uint8_t,FIFO_WIDTH_BYTES> mem_ref[FIFO_DEPTH];

public:
    bool*                     write_out      = &write_reg;
    logic<FIFO_WIDTH_BYTES*8>* data_out       = &data_reg;

    bool*                     read_out       = &read_reg;
    logic<FIFO_WIDTH_BYTES*8>* data_in        = nullptr;

    bool*            empty_in = nullptr;
    bool*            full_in  = nullptr;
    bool*            clear_out  = &clear_reg;
    bool*            afull_in = nullptr;

    void connect()
    {
        fifo.inst_name = inst_name + "/fifo";

        fifo.write_in      = write_out;
        fifo.data_in       = data_out;
        fifo.read_in       = read_out;
        fifo.clear_in      = clear_out;
        fifo.connect();

        data_in           = fifo.data_out;
        empty_in          = fifo.empty_out;
        full_in           = fifo.full_out;
        afull_in          = fifo.afull_out;
    }

    void work(bool clk, bool reset)
    {
        if (!clk) return;
        fifo.work(clk, reset);

        if (reset) {
            clear_reg.clr();
            read_addr.clr();
            write_addr.clr();
            was_read.clr();
            return;
        }

        write_reg.next = 0;
        if (to_write_cnt) {
            write_addr.next = write_addr + 1;
            write_reg.next = 1;
            data_reg.next = *(logic<FIFO_WIDTH_BYTES*8>*)&mem_ref[write_addr];
            to_write_cnt.next = to_write_cnt - 1;

            to_read_cnt.next = std::min((unsigned)random()%30, (unsigned)to_write_cnt);
        }
        read_reg.next = 0;
        if (to_read_cnt) {
            to_read_cnt.next = to_read_cnt - 1;
            read_reg.next = 1;
        }
        if (!to_write_cnt) {
            to_write_cnt.next = random()%100;
        }
        if (memcmp(data_in, &mem_ref[read_addr], sizeof(data_in)) != 0 && ((SHOWAHEAD && read_reg) || (!SHOWAHEAD && was_read))) {
            std::print("{}: {} was read instead of {} from address {}\n",
                inst_name,
                *(logic<FIFO_WIDTH_BYTES*8>*)data_in,
                *(logic<FIFO_WIDTH_BYTES*8>*)&mem_ref[read_addr],
                read_addr);
            error = true;
        }
        was_read.next = 0;
        if (read_reg && !SHOWAHEAD) {
            was_read.next = 1;
        }
        if ((read_reg && SHOWAHEAD) || (was_read && !SHOWAHEAD)) {
            read_addr.next = read_addr + 1;
        }
    }

    void strobe()
    {
        fifo.strobe();

        read_addr.strobe();
        write_addr.strobe();
        data_reg.strobe();
        write_reg.strobe();
        read_reg.strobe();
        was_read.strobe();

        clear_reg.strobe();
        to_write_cnt.strobe();
        to_read_cnt.strobe();
    }

    void comb()
    {
        fifo.comb();
        fifo.full_comb_func();
        fifo.empty_comb_func();
    }

    bool run()
    {
        for (size_t i=0; i < FIFO_DEPTH; ++i) {
            for (size_t j=0; j < FIFO_WIDTH_BYTES; ++j) {
                mem_ref[i][j] = random();
            }
        }
        std::print("TestFifo, FIFO_WIDTH_BYTES: {}, FIFO_DEPTH: {}, SHOWAHEAD: {}...", FIFO_WIDTH_BYTES, FIFO_DEPTH, SHOWAHEAD);
        inst_name = "fifo_test";
        connect();
        work(1, 1);
        int cycles = 10000;
        int clk = 0;
        while (--cycles && !error) {
            comb();
            work(clk, 0);
            strobe();
            clk = !clk;
        }
        std::print(" {}\n", !error?"PASSED":"FAILED");
        return !error;
    }
};

#ifndef NO_MAINFILE
int main (int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        g_debugen = 1;
    }
    TestFifo<32,1024,true>().run();
    TestFifo<32,1024,false>().run();
}
#endif

#endif  //SYNTHESIS

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
