#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

class Fifo2clk : public Module
{
    static constexpr size_t DEPTH = 16;
    static constexpr size_t ADDR_BITS = 4;
    static constexpr size_t PTR_BITS = ADDR_BITS + 1;

public:
    _PORT(bool) write_valid_in;
    _PORT(u<8>) write_data_in;
    _PORT(bool) write_ready_out = _ASSIGN_COMB(write_ready_comb_func());

    _PORT(bool) read_ready_in;
    _PORT(bool) read_valid_out = _ASSIGN_COMB(read_valid_comb_func());
    _PORT(u<8>) read_data_out = _ASSIGN_COMB(read_data_comb_func());

private:
    memory<u8, 1, DEPTH> data_mem;

    reg<u<PTR_BITS>> write_bin_reg;
    reg<u<PTR_BITS>> write_gray_reg;
    reg<u<PTR_BITS>> read_gray_write1_reg;
    reg<u<PTR_BITS>> read_gray_write2_reg;

    reg<u<PTR_BITS>> read_bin_reg;
    reg<u<PTR_BITS>> read_gray_reg;
    reg<u<PTR_BITS>> write_gray_read1_reg;
    reg<u<PTR_BITS>> write_gray_read2_reg;

    bool write_ready_comb;
    bool& write_ready_comb_func()
    {
        const u<PTR_BITS> full_gray = read_gray_write2_reg
            ^ u<PTR_BITS>((1u << ADDR_BITS) | (1u << (ADDR_BITS - 1)));
        write_ready_comb = write_gray_reg != full_gray;
        return write_ready_comb;
    }

    bool read_valid_comb;
    bool& read_valid_comb_func()
    {
        read_valid_comb = read_gray_reg != write_gray_read2_reg;
        return read_valid_comb;
    }

    u<8> read_data_comb;
    u<8>& read_data_comb_func()
    {
        read_data_comb = (uint8_t)data_mem[(uint32_t)read_bin_reg & (uint32_t)(DEPTH - 1)];
        return read_data_comb;
    }

public:
    void _work_write_clk(bool reset)
    {
        u<PTR_BITS> next;

        read_gray_write1_reg._next = read_gray_reg;
        read_gray_write2_reg._next = read_gray_write1_reg;
        if (write_valid_in() && write_ready_comb_func()) {
            data_mem[(uint32_t)write_bin_reg & (uint32_t)(DEPTH - 1)] = write_data_in();
            next = write_bin_reg + 1;
            write_bin_reg._next = next;
            write_gray_reg._next = next ^ (next >> 1);
        }
        if (reset) {
            write_bin_reg.clr();
            write_gray_reg.clr();
            read_gray_write1_reg.clr();
            read_gray_write2_reg.clr();
        }
    }

    void _strobe_write_clk()
    {
        data_mem.apply();
        write_bin_reg.strobe();
        write_gray_reg.strobe();
        read_gray_write1_reg.strobe();
        read_gray_write2_reg.strobe();
    }

    void _work_neg_write_clk(bool) {}
    void _strobe_neg_write_clk() {}

    void _work_read_clk(bool reset)
    {
        u<PTR_BITS> next;

        write_gray_read1_reg._next = write_gray_reg;
        write_gray_read2_reg._next = write_gray_read1_reg;
        if (read_ready_in() && read_valid_comb_func()) {
            next = read_bin_reg + 1;
            read_bin_reg._next = next;
            read_gray_reg._next = next ^ (next >> 1);
        }
        if (reset) {
            read_bin_reg.clr();
            read_gray_reg.clr();
            write_gray_read1_reg.clr();
            write_gray_read2_reg.clr();
        }
    }

    void _strobe_read_clk()
    {
        read_bin_reg.strobe();
        read_gray_reg.strobe();
        write_gray_read1_reg.strobe();
        write_gray_read2_reg.strobe();
    }

    void _work_neg_read_clk(bool) {}
    void _strobe_neg_read_clk() {}

    void _assign() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>

#include "../tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static bool generated_sv_has_fifo_clock_domains()
{
#ifdef VERILATOR
    const std::filesystem::path sv_path = "Fifo2clk/Fifo2clk.sv";
#else
    const std::filesystem::path sv_path = "generated/Fifo2clk.sv";
#endif
    std::ifstream input(sv_path);
    if (!input) {
        std::print("can't open generated FIFO RTL: {}\n", sv_path.string());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const size_t write_task_begin = text.find("task _work_write_clk");
    const size_t read_task_begin = text.find("task _work_read_clk");
    const size_t write_begin = text.find("always_ff @(posedge write_clk)");
    const size_t read_begin = text.find("always_ff @(posedge read_clk)");
    if (write_task_begin == std::string::npos || read_task_begin == std::string::npos
        || write_begin == std::string::npos || read_begin == std::string::npos) {
        return false;
    }
    const std::string write_task = text.substr(write_task_begin, read_task_begin - write_task_begin);
    const std::string read_task = text.substr(read_task_begin, write_begin - read_task_begin);
    return text.find(" data_mem[16];") != std::string::npos
        && write_task.find("data_mem[") != std::string::npos
        && read_task.find("data_mem[") == std::string::npos
        && text.find("data_mem_tmp") == std::string::npos;
}

class Fifo2clkTest
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    Fifo2clk dut;
    bool write_valid = false;
    u<8> write_data = 0;
    bool read_ready = false;
#endif
    std::deque<uint8_t> expected;
    uint8_t next_data = 1;
    unsigned reads = 0;

    void set_write(bool valid, uint8_t data, bool reset)
    {
#ifdef VERILATOR
        dut.reset = reset;
        dut.write_valid_in = valid;
        dut.write_data_in = data;
#else
        write_valid = valid;
        write_data = data;
#endif
    }

    void set_read(bool ready, bool reset)
    {
#ifdef VERILATOR
        dut.reset = reset;
        dut.read_ready_in = ready;
#else
        read_ready = ready;
#endif
    }

    bool write_ready()
    {
#ifdef VERILATOR
        return dut.write_ready_out;
#else
        return dut.write_ready_out();
#endif
    }

    bool read_valid()
    {
#ifdef VERILATOR
        return dut.read_valid_out;
#else
        return dut.read_valid_out();
#endif
    }

    uint8_t read_data()
    {
#ifdef VERILATOR
        return dut.read_data_out;
#else
        return (uint8_t)dut.read_data_out();
#endif
    }

    void write_edge(bool reset, bool valid)
    {
        set_write(valid, next_data, reset);
#ifdef VERILATOR
        dut.write_clk = 0;
        dut.eval();
#endif
        const bool accepted = !reset && valid && write_ready();
#ifdef VERILATOR
        dut.write_clk = 1;
        dut.eval();
#else
        dut._work_write_clk(reset);
        dut._strobe_write_clk();
#endif
        if (accepted) {
            expected.push_back(next_data++);
        }
        ++_system_clock;
    }

    bool read_edge(bool reset, bool ready)
    {
        set_read(ready, reset);
#ifdef VERILATOR
        dut.read_clk = 0;
        dut.eval();
#endif
        const bool accepted = !reset && ready && read_valid();
        const uint8_t value = accepted ? read_data() : 0;
#ifdef VERILATOR
        dut.read_clk = 1;
        dut.eval();
#else
        dut._work_read_clk(reset);
        dut._strobe_read_clk();
#endif
        if (accepted) {
            if (expected.empty() || expected.front() != value) {
                std::print("FIFO mismatch: got {}, expected {}\n", value,
                    expected.empty() ? 0 : expected.front());
                return false;
            }
            expected.pop_front();
            ++reads;
        }
        ++_system_clock;
        return true;
    }

public:
    bool run()
    {
#ifndef VERILATOR
        dut.write_valid_in = _ASSIGN(write_valid);
        dut.write_data_in = _ASSIGN(write_data);
        dut.read_ready_in = _ASSIGN(read_ready);
        dut._assign();
#else
        dut.write_clk = 0;
        dut.read_clk = 0;
#endif
        for (unsigned i = 0; i < 2; ++i) {
            write_edge(true, false);
            if (!read_edge(true, false)) {
                return false;
            }
        }

        for (unsigned step = 0; step < 240; ++step) {
            if ((step & 1u) == 0) {
                write_edge(false, (step % 10) != 8);
            }
            if ((step % 4) == 1 && !read_edge(false, (step % 12) != 9)) {
                return false;
            }
        }
        for (unsigned step = 0; step < 200 && !expected.empty(); ++step) {
            if ((step & 1u) == 0) {
                write_edge(false, false);
            }
            if ((step % 3) == 1 && !read_edge(false, true)) {
                return false;
            }
        }
        if (!expected.empty() || reads < 32) {
            std::print("FIFO did not drain: remaining={}, reads={}\n", expected.size(), reads);
            return false;
        }
        return generated_sv_has_fifo_clock_domains();
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        noveril |= std::strcmp(argv[i], "--noveril") == 0;
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        ok = VerilatorCompileInExactFolder(__FILE__, "Fifo2clk", "Fifo2clk",
            {"Predef_pkg"}, {"../../../../include"});
        ok = ok && std::system("Fifo2clk/obj_dir/VFifo2clk") == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif
    ok = ok && Fifo2clkTest().run();
    std::print("Two-clock FIFO {}\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
