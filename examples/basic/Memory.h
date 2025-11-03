#pragma once

#include "cpphdl.h"

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
        data_out_comb = buffer[*read_addr_in];
        if (!SHOWAHEAD) {
            data_out_comb = data_out_reg;
        }
    }

    void work(bool clk, bool reset)
    {
        if (!clk) return;

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
    Memory<MEM_WIDTH_BYTES,MEM_DEPTH> mem;

    reg<u<clog2(MEM_DEPTH)>>       write_addr_reg;
    reg<logic<MEM_WIDTH_BYTES*8>>  data_reg;
    reg<u1>                        write_reg;
    reg<u<clog2(MEM_DEPTH)>>       read_addr_reg;

    reg<u1> clean;
    reg<u1> test;
    reg<u16> to_write;
    reg<u16> to_read;
    bool error = false;

    std::array<std::array<uint8_t,MEM_WIDTH_BYTES>,MEM_DEPTH> mem_copy;

public:
    u<clog2(MEM_DEPTH)>*      write_addr_out = &write_addr_reg;
    bool*                     write_out      = &write_reg;
    logic<MEM_WIDTH_BYTES*8>* data_out       = &data_reg;

    u<clog2(MEM_DEPTH)>*      read_addr_out  = &read_addr_reg;
    logic<MEM_WIDTH_BYTES*8>* data_in        = nullptr;

    void connect()
    {
        mem.write_addr_in = write_addr_out;
        mem.write_in      = write_out;
        mem.data_in       = data_out;

        mem.read_addr_in  = read_addr_out;
        data_in           = mem.data_out;
    }

    void work(bool clk_in, bool reset_in)
    {
        if (!clk_in) return;

        if (reset_in) {
            clean.set(1);
            test.clr();
            to_write.clr();
            return;
        }

        write_reg.next = 0;
        if (clean) {
            if (to_write != MEM_DEPTH) {
                write_addr_reg.next = to_write;
                to_write.next = to_write + 1;
                write_reg.next = 1;
            }
            else {
                clean.next = 0;
                test.next = 1;
                to_write.next = 0;
                to_read.next = 0;
            }
        }
        else {
            if (to_write) {
                write_addr_reg.next = random()%MEM_DEPTH;
                write_reg.next = 1;
                data_reg.next = random();
                to_write.next = to_write - 1;
            }
            if (to_read) {
                write_addr_reg.next = random()%MEM_DEPTH;
                to_read.next = to_read - 1;
            }
            if (test) {
                to_write.next = random()%100;
                to_read.next = std::min((unsigned)random()%100, (unsigned)to_write.next);
            }
        }
        if (write_reg) {
            memcpy(&mem_copy[write_addr_reg], &data_reg, sizeof(mem_copy[write_addr_reg]));
        }
        if (memcmp(&data_in, &mem_copy[read_addr_reg], sizeof(data_in)) == 0) {
            std::print("{} was read instead of {} from address {}\n", *(logic<MEM_WIDTH_BYTES*8>*)data_in, mem_copy[read_addr_reg], read_addr_reg);
            error = true;
        }
    }

    void strobe()
    {
        write_addr_reg.strobe();
        data_reg.strobe();
        write_reg.strobe();
        read_addr_reg.strobe();

        clean.strobe();
        test.strobe();
        to_write.strobe();
        to_read.strobe();

        mem.strobe();
    }

    void comb()
    {
        mem.comb();
        mem.data_out_comb_func();
    }

    bool run()
    {
        int clk = 0;
        int cycles = 10000;
        while (--cycles && !error) {
            comb();
            work(clk);
            strobe();
            clk = !clk;
        }
        return !error;
    }
};

#endif
