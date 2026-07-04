#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <print>

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

class LogicBitsWiden : public Module
{
public:
    _PORT(logic<32>) src_in;
    _PORT(logic<34>) full_ctor_out = _ASSIGN_COMB(full_ctor_comb_func());
    _PORT(logic<34>) low_ctor_out = _ASSIGN_COMB(low_ctor_comb_func());
    _PORT(logic<34>) assign_out = _ASSIGN_COMB(assign_comb_func());

private:
    logic<34> full_ctor_comb;
    logic<34> low_ctor_comb;
    logic<34> assign_comb;

    logic<34>& full_ctor_comb_func()
    {
        full_ctor_comb = logic<34>(src_in().bits(31, 0));
        return full_ctor_comb;
    }

    logic<34>& low_ctor_comb_func()
    {
        low_ctor_comb = logic<34>(src_in().bits(15, 0));
        return low_ctor_comb;
    }

    logic<34>& assign_comb_func()
    {
        assign_comb = 0;
        assign_comb = src_in().bits(31, 0);
        return assign_comb;
    }

public:
    void _work(bool reset)
    {
    }

    void _strobe() {}
    void _assign() {}
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

template<typename T>
static T verilator_read(const void* ptr)
{
    T value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

static bool check_value(const char* name, const logic<34>& got, uint64_t expected)
{
    const uint64_t got64 = (uint64_t)got;
    if (got64 != expected) {
        std::print("\n{} ERROR: got 0x{:x}, expected 0x{:x}\n", name, got64, expected);
        return false;
    }
    return true;
}

static bool check_direct_construction()
{
    bool ok = true;

    logic<32> source = 0x80000004ull;
    logic<34> widened(source.bits(31, 0));
    logic<34> assigned;
    assigned = source.bits(31, 0);
    logic<64> wider64(source.bits(31, 0));
    logic<34> low16(source.bits(15, 0));

    ok &= check_value("direct widened logic_bits constructor", widened, 0x80000004ull);
    ok &= check_value("logic_bits assignment", assigned, 0x80000004ull);
    if ((uint64_t)wider64 != 0x80000004ull) {
        std::print("\nwider64 ERROR: got 0x{:x}, expected 0x80000004\n", (uint64_t)wider64);
        ok = false;
    }
    ok &= check_value("partial slice widened constructor", low16, 0x0004ull);

    return ok;
}

class TestLogicBitsWiden : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    LogicBitsWiden dut;
#endif

    logic<32> source;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.src_in = _ASSIGN_REG(source);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.src_in = (uint32_t)source;
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

    logic<34> full_ctor()
    {
#ifdef VERILATOR
        return verilator_read<logic<34>>(&dut.full_ctor_out);
#else
        return dut.full_ctor_out();
#endif
    }

    logic<34> low_ctor()
    {
#ifdef VERILATOR
        return verilator_read<logic<34>>(&dut.low_ctor_out);
#else
        return dut.low_ctor_out();
#endif
    }

    logic<34> assign_value()
    {
#ifdef VERILATOR
        return verilator_read<logic<34>>(&dut.assign_out);
#else
        return dut.assign_out();
#endif
    }

    void check(const char* name, const logic<34>& got, uint64_t expected)
    {
        error |= !check_value(name, got, expected);
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestLogicBitsWiden...");
#else
        std::print("CppHDL TestLogicBitsWiden...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "logic_bits_widen_test";
        _assign();

#ifndef VERILATOR
        error |= !check_direct_construction();
#endif

        source = 0x80000004ull;
        eval(true);
        neg(true);
        ++_system_clock;

        source = 0x80000004ull;
        eval(false);
        check("module full constructor", full_ctor(), 0x80000004ull);
        check("module assignment", assign_value(), 0x80000004ull);
        check("module partial constructor", low_ctor(), 0x0004ull);
        neg(false);
        ++_system_clock;

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
        ok &= VerilatorCompile(__FILE__, "LogicBitsWiden", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("LogicBitsWiden_1/obj_dir/VLogicBitsWiden") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestLogicBitsWiden().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
