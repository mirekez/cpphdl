#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

// Verifies that cpphdl preserves and accepts:
// 1. A return stored directly in a CaseStmt.
// 2. A return stored directly in a DefaultStmt.
// 3. A nested switch whose cases all return from the function.
class SwitchReturn : public Module
{
public:
    _PORT(u<2>) selector_in;
    _PORT(u<2>) nested_selector_in;
    _PORT(u<32>) value_out = _ASSIGN_COMB(value_comb_func());
    _PORT(u<32>) break_value_out = _ASSIGN_COMB(break_value_comb_func());

private:
    u<32> value_comb;
    u<32> break_value_comb;

    u<32> select_value(u<2> selector, u<2> nested_selector)
    {
        switch (selector) {
            case 0: return 0x11;
            case 1:
                switch (nested_selector) {
                    case 0: return 0x20;
                    case 1: return 0x21;
                    default: return 0x2f;
                }
            case 2: return 0x33;
            default: return 0xff;
        }
        return 0xee;
    }

    u<32> select_break_value(u<2> selector)
    {
        u<32> result = 0xaa;
        switch (selector) {
            case 0: result = 0x44; break;
            case 1: result = 0x55; break;
            case 2: break;
            default: result = 0x66; break;
        }
        return result;
    }

    u<32>& value_comb_func()
    {
        value_comb = select_value(selector_in(), nested_selector_in());
        return value_comb;
    }

    u<32>& break_value_comb_func()
    {
        break_value_comb = select_break_value(selector_in());
        return break_value_comb;
    }

public:
    void _work(bool reset)
    {
    }

    void _strobe()
    {
    }

    void _assign()
    {
    }
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

class TestSwitchReturn : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    SwitchReturn dut;
#endif
    u<2> selector;
    u<2> nested_selector;

    struct TestCase {
        uint8_t selector;
        uint8_t nested_selector;
        uint32_t expected;
        uint32_t expected_break;
    };

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.selector_in = _ASSIGN_REG(selector);
        dut.nested_selector_in = _ASSIGN_REG(nested_selector);
        dut._assign();
#endif
    }

    uint32_t output()
    {
#ifdef VERILATOR
        return (uint32_t)dut.value_out;
#else
        return (uint32_t)dut.value_out();
#endif
    }

    bool run()
    {
        const TestCase cases[] = {
            {0, 0, 0x11, 0x44},
            {1, 0, 0x20, 0x55},
            {1, 1, 0x21, 0x55},
            {1, 3, 0x2f, 0x55},
            {2, 0, 0x33, 0xaa},
            {3, 0, 0xff, 0x66},
        };
        _assign();

        for (const TestCase& test : cases) {
            selector = test.selector;
            nested_selector = test.nested_selector;
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = 0;
            dut.selector_in = test.selector;
            dut.nested_selector_in = test.nested_selector;
            dut.eval();
#endif
            ++_system_clock;
            if (output() != test.expected) {
                return false;
            }
#ifdef VERILATOR
            if ((uint32_t)dut.break_value_out != test.expected_break) {
                return false;
            }
#else
            if ((uint32_t)dut.break_value_out() != test.expected_break) {
                return false;
            }
#endif
        }
        return true;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "SwitchReturn", {"Predef_pkg"}, {"../../../../include"});
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("SwitchReturn/obj_dir/VSwitchReturn") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok && TestSwitchReturn().run() ? 0 : 1;
}

#endif
