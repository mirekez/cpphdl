#pragma once
#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

static bool g_debugen = 0;
#define DEBUG(a...) if (g_debugen) { std::print(a); }

using namespace cpphdl;

template<size_t MEM_WIDTH_BYTES, size_t MEM_DEPTH, bool SHOWAHEAD = true>
class Memory : public Module
{
    logic<MEM_WIDTH_BYTES*8> data_out_comb;
    reg<logic<MEM_WIDTH_BYTES*8>> data_out_reg;
    memory<u8,MEM_WIDTH_BYTES,MEM_DEPTH> buffer;

    size_t i;

public:
    u<clog2(MEM_DEPTH)>*      write_addr_in   = nullptr;
    bool*                     write_in        = nullptr;
    logic<MEM_WIDTH_BYTES*8>* data_in         = nullptr;

    u<clog2(MEM_DEPTH)>*      read_addr_in    = nullptr;
    bool*                     read_in         = nullptr;
    logic<MEM_WIDTH_BYTES*8>* data_out        = &data_out_comb;

    void connect() {}

    void data_out_comb_func()
    {
        if (SHOWAHEAD) {
            data_out_comb = buffer[*read_addr_in];
        }
        else {
            data_out_comb = data_out_reg;
        }
    }

    void work(bool clk, bool reset)
    {
        if (!clk) return;

        DEBUG("{}: input: ({}){}@{}, output: ({}){}@{}\n", inst_name, (int)*write_in, *data_in, *write_addr_in, (int)*read_in, *data_out, *read_addr_in);

        if (*write_in) {
            buffer[*write_addr_in] = *data_in;
        }
        if (!SHOWAHEAD) {
            data_out_reg.next = buffer[*read_addr_in];
        }
    }

    void strobe()
    {
        buffer.apply();
        data_out_reg.strobe();
    }

    void comb() {}
};

template struct Memory<32,1024>;

#ifndef SYNTHESIS

template<size_t MEM_WIDTH_BYTES, size_t MEM_DEPTH, bool SHOWAHEAD>
class TestMemory : Module
{
    Memory<MEM_WIDTH_BYTES,MEM_DEPTH,SHOWAHEAD> mem;

    reg<u<clog2(MEM_DEPTH)>>       write_addr_reg;
    reg<logic<MEM_WIDTH_BYTES*8>>  data_reg;
    reg<u1>                        write_reg;
    reg<u<clog2(MEM_DEPTH)>>       read_addr_reg;
    reg<u1>                        read_reg;
    reg<u1>                  clean;
    reg<u1>                  was_read;
    reg<u<clog2(MEM_DEPTH)>> was_read_addr;
    reg<u16>                 to_write_cnt;
    reg<u16>                 to_read_cnt;
    bool                     error = false;

    std::array<std::array<uint8_t,MEM_WIDTH_BYTES>,MEM_DEPTH> mem_copy;

public:
    u<clog2(MEM_DEPTH)>*      write_addr_out = &write_addr_reg;
    bool*                     write_out      = &write_reg;
    logic<MEM_WIDTH_BYTES*8>* data_out       = &data_reg;

    u<clog2(MEM_DEPTH)>*      read_addr_out  = &read_addr_reg;
    bool*                     read_out       = &read_reg;
    logic<MEM_WIDTH_BYTES*8>* data_in        = nullptr;

    void connect()
    {
        mem.inst_name = inst_name + "/mem";

        mem.write_addr_in = write_addr_out;
        mem.write_in      = write_out;
        mem.data_in       = data_out;
        mem.read_addr_in  = read_addr_out;
        mem.read_in       = read_out;
        mem.connect();

        data_in           = mem.data_out;
    }

    void work(bool clk, bool reset)
    {
        if (!clk) return;
        mem.work(clk, reset);

        if (reset) {
            clean.set(1);
            to_write_cnt.clr();
            was_read.clr();
            return;
        }

        write_reg.next = 0;
        if (clean) {
            if (to_write_cnt != MEM_DEPTH) {
                write_addr_reg.next = to_write_cnt;
                to_write_cnt.next = to_write_cnt + 1;
                write_reg.next = 1;
            }
            else {
                clean.next = 0;
                to_write_cnt.next = 0;
                to_read_cnt.next = 0;
            }
        }
        else {
            if (to_write_cnt) {
                write_addr_reg.next = random()%MEM_DEPTH;
                write_reg.next = 1;
                data_reg.next = random();
                to_write_cnt.next = to_write_cnt - 1;
            }
            read_reg.next = 0;
            if (to_read_cnt) {
                read_addr_reg.next = random()%MEM_DEPTH;
                to_read_cnt.next = to_read_cnt - 1;
                if (!SHOWAHEAD) {
                    read_reg.next = 1;
                }
            }
            if (!to_write_cnt && !to_read_cnt) {
                to_write_cnt.next = random()%100;
                to_read_cnt.next = std::min((unsigned)random()%30, (unsigned)to_write_cnt.next);
            }
        }
        if (memcmp(data_in, &mem_copy[SHOWAHEAD?read_addr_reg:was_read_addr], sizeof(data_in)) != 0 && (was_read || SHOWAHEAD)) {
            std::print("{}: {} was read instead of {} from address {}\n",
                inst_name,
                *(logic<MEM_WIDTH_BYTES*8>*)data_in,
                *(logic<MEM_WIDTH_BYTES*8>*)&mem_copy[read_addr_reg],
                SHOWAHEAD?read_addr_reg:was_read_addr);
            error = true;
        }
        if (was_read) {
            was_read_addr.next = read_addr_reg;
        }
        if (write_reg) {  // after check
            memcpy(&mem_copy[write_addr_reg], &data_reg, sizeof(mem_copy[write_addr_reg]));
        }
    }

    void strobe()
    {
        mem.strobe();

        write_addr_reg.strobe();
        data_reg.strobe();
        write_reg.strobe();
        read_addr_reg.strobe();
        read_reg.strobe();

        clean.strobe();
        was_read.strobe();
        was_read_addr.strobe();
        to_write_cnt.strobe();
        to_read_cnt.strobe();
    }

    void comb()
    {
        mem.comb();
        mem.data_out_comb_func();
    }

    bool run()
    {
        std::print("TestMemory, MEM_WIDTH_BYTES: {}, MEM_DEPTH: {}, SHOWAHEAD: {}...", MEM_WIDTH_BYTES, MEM_DEPTH, SHOWAHEAD);
        inst_name = "mem_test";
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
    TestMemory<32,1024,true>().run();
    TestMemory<32,1024,false>().run();
}
#endif

#endif  //SYNTHESIS

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
