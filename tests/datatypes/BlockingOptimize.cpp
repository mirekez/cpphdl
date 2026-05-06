#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

class BlockingOptimize : public Module
{
public:
    __PORT(bool)  enable_in;
    __PORT(u<32>) data_in;
    __PORT(u<32>) value_out = __VAR(value_comb_func());

private:
    reg<u<32>> once_accessed_reg;
    u<32> value_comb;

    void update_once_accessed(bool reset)
    {
        once_accessed_reg._next = reset ? u<32>(0) : u<32>(data_in() + (enable_in() ? 0x1234 : 0x10));
    }

    u<32>& value_comb_func()
    {
        value_comb = once_accessed_reg;
        return value_comb;
    }

public:
    void _work(bool reset)
    {
        update_once_accessed(reset);
    }

    void _strobe()
    {
        once_accessed_reg.strobe();
    }

    void _assign() {}
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

template<typename T>
static T verilator_read(const void* ptr)
{
    T value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

static u<32> expected_value(bool reset, bool enable, uint32_t data)
{
    return reset ? 0 : data + (enable ? 0x1234 : 0x10);
}

#ifdef VERILATOR
static bool generated_sv_has_once_accessed_nonblocking()
{
    const std::filesystem::path sv_path = "BlockingOptimize_1/BlockingOptimize.sv";
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool has_direct_nonblocking =
        text.find("once_accessed_reg <=") != std::string::npos;
    const bool has_tmp_staging =
        text.find("once_accessed_reg_tmp") != std::string::npos;

    if (!has_direct_nonblocking || has_tmp_staging) {
        std::print("\nERROR: once_accessed_reg was not emitted as direct non-blocking assignment\n");
        std::print("       direct <= found: {}, tmp staging found: {}\n",
            has_direct_nonblocking, has_tmp_staging);
        return false;
    }
    return true;
}
#endif

class TestBlockingOptimize : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    BlockingOptimize dut;
#endif

    bool enable = false;
    u<32> data;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.enable_in = __VAR(enable);
        dut.data_in = __VAR(data);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.enable_in = enable;
        dut.data_in = data;
        dut.clk = 1;
        dut.reset = reset;
        dut.eval();
#else
        dut._work(reset);
#endif
    }

    void neg(bool reset)
    {
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
#else
        (void)reset;
#endif
    }

    void strobe()
    {
#ifndef VERILATOR
        dut._strobe();
#endif
    }

    u<32> value()
    {
#ifdef VERILATOR
        return verilator_read<u<32>>(&dut.value_out);
#else
        return dut.value_out();
#endif
    }

    void cycle(bool reset)
    {
        eval(reset);
        strobe();
        neg(reset);
        ++sys_clock;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestBlockingOptimize...");
#else
        std::print("CppHDL TestBlockingOptimize...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "blocking_optimize_test";
        _assign();

#ifdef VERILATOR
        error |= !generated_sv_has_once_accessed_nonblocking();
#endif

        data = 0x11110000;
        enable = false;
        cycle(true);
        if (value() != 0) {
            std::print("\nreset ERROR: got {}, expected 0\n", value());
            error = true;
        }

        for (uint32_t i = 0; i < 64 && !error; ++i) {
            data = 0x20000000u + i * 0x10203u;
            enable = (i & 1) != 0;
            u<32> expected = expected_value(false, enable, (uint32_t)data);
            cycle(false);
            if (value() != expected) {
                std::print("\nvalue ERROR at {}: got {}, expected {}\n", i, value(), expected);
                error = true;
            }
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "BlockingOptimize", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("BlockingOptimize_1/obj_dir/VBlockingOptimize") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestBlockingOptimize().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
