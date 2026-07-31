#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

class AsyncReset : public Module
{
public:
    _PORT(bool) fast_enable_in;
    _PORT(bool) fast_neg_enable_in;
    _PORT(bool) slow_enable_in;

    _PORT(u<8>) fast_count_out = _ASSIGN_REG(fast_count_reg);
    _PORT(u<8>) fast_neg_count_out = _ASSIGN_REG(fast_neg_count_reg);
    _PORT(u<8>) slow_count_out = _ASSIGN_REG(slow_count_reg);

private:
    reg<u<8>> fast_count_reg;
    reg<u<8>> fast_neg_count_reg;
    reg<u<8>> slow_count_reg;

public:
    void _work_fast_clk(bool)
    {
        if (fast_enable_in()) {
            fast_count_reg._next = fast_count_reg + 1;
        }
    }

    void _strobe_fast_clk()
    {
        fast_count_reg.strobe();
    }

    void _reset_pos_fast_clk()
    {
        fast_count_reg.clr();
    }

    void _work_neg_fast_clk(bool)
    {
        if (fast_neg_enable_in()) {
            fast_neg_count_reg._next = fast_neg_count_reg + 1;
        }
    }

    void _strobe_neg_fast_clk()
    {
        fast_neg_count_reg.strobe();
    }

    void _reset_neg_fast_clk()
    {
        fast_neg_count_reg.clr();
    }

    void _work_slow_clk(bool)
    {
        if (slow_enable_in()) {
            slow_count_reg._next = slow_count_reg + 1;
        }
    }

    void _strobe_slow_clk()
    {
        slow_count_reg.strobe();
    }

    void _reset_pos_slow_clk()
    {
        slow_count_reg.clr();
    }

    void _assign() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>

#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

#ifdef VERILATOR
#define RESET_VALUE(name) (dut.name)
#else
#define RESET_VALUE(name) (dut.name())
#endif

class AsyncResetTest
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    AsyncReset dut;
#endif
    bool fast_enable = false;
    bool fast_neg_enable = false;
    bool slow_enable = false;
    bool reset = false;
    std::string generated_sv;

    bool check(bool condition, const char* feature)
    {
        if (!condition) {
            std::print("Asynchronous reset failed: {}\n", feature);
        }
        return condition;
    }

    void drive_inputs()
    {
#ifdef VERILATOR
        dut.reset = reset;
        dut.fast_enable_in = fast_enable;
        dut.fast_neg_enable_in = fast_neg_enable;
        dut.slow_enable_in = slow_enable;
#endif
    }

    void evaluate()
    {
#ifdef VERILATOR
        drive_inputs();
        dut.eval();
#endif
    }

    void fast_posedge()
    {
#ifdef VERILATOR
        dut.fast_clk = 1;
        evaluate();
#else
        if (reset) {
            dut._reset_pos_fast_clk();
        }
        else {
            dut._work_fast_clk(false);
        }
        dut._strobe_fast_clk();
#endif
        ++_system_clock;
    }

    void fast_negedge()
    {
#ifdef VERILATOR
        dut.fast_clk = 0;
        evaluate();
#else
        if (reset) {
            dut._reset_neg_fast_clk();
        }
        else {
            dut._work_neg_fast_clk(false);
        }
        dut._strobe_neg_fast_clk();
#endif
        ++_system_clock;
    }

    void slow_posedge()
    {
#ifdef VERILATOR
        dut.slow_clk = 1;
        evaluate();
#else
        if (reset) {
            dut._reset_pos_slow_clk();
        }
        else {
            dut._work_slow_clk(false);
        }
        dut._strobe_slow_clk();
#endif
        ++_system_clock;
    }

    void slow_negedge()
    {
#ifdef VERILATOR
        dut.slow_clk = 0;
        evaluate();
#endif
        ++_system_clock;
    }

    void fast_cycle()
    {
        fast_posedge();
        fast_negedge();
    }

    void slow_cycle()
    {
        slow_posedge();
        slow_negedge();
    }

    void assert_reset_asynchronously()
    {
        reset = true;
#ifdef VERILATOR
        evaluate();
#else
        dut._reset_pos_fast_clk();
        dut._strobe_fast_clk();
        dut._reset_neg_fast_clk();
        dut._strobe_neg_fast_clk();
        dut._reset_pos_slow_clk();
        dut._strobe_slow_clk();
#endif
        ++_system_clock;
    }

    void release_reset_asynchronously()
    {
        reset = false;
        evaluate();
        ++_system_clock;
    }

    bool load_generated_sv()
    {
        if (!generated_sv.empty()) {
            return true;
        }
#ifdef VERILATOR
        const std::filesystem::path path = "AsyncReset/AsyncReset.sv";
#else
        const std::filesystem::path path = "generated/AsyncReset.sv";
#endif
        std::ifstream input(path);
        if (!input) {
            std::print("can't open generated asynchronous-reset RTL: {}\n", path.string());
            return false;
        }
        generated_sv.assign(std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
        return true;
    }

    bool test_generated_async_reset_blocks()
    {
        if (!load_generated_sv()) {
            return false;
        }
        return check(
            generated_sv.find("always_ff @(posedge fast_clk or posedge reset)")
                    != std::string::npos
                && generated_sv.find("always_ff @(negedge fast_clk or posedge reset)")
                    != std::string::npos
                && generated_sv.find("always_ff @(posedge slow_clk or posedge reset)")
                    != std::string::npos
                && generated_sv.find("_reset_pos_fast_clk();") != std::string::npos
                && generated_sv.find("_reset_neg_fast_clk();") != std::string::npos
                && generated_sv.find("_reset_pos_slow_clk();") != std::string::npos,
            "generated asynchronous sensitivity and reset handlers");
    }

    bool test_async_assertion_without_clock_edges()
    {
        assert_reset_asynchronously();
        release_reset_asynchronously();

        fast_enable = true;
        fast_neg_enable = true;
        slow_enable = true;
        for (unsigned i = 0; i < 3; ++i) {
            fast_cycle();
        }
        for (unsigned i = 0; i < 2; ++i) {
            slow_cycle();
        }
        if (!check((uint8_t)RESET_VALUE(fast_count_out) == 3
                && (uint8_t)RESET_VALUE(fast_neg_count_out) == 3
                && (uint8_t)RESET_VALUE(slow_count_out) == 2,
                "state advances before asynchronous assertion")) {
            return false;
        }

        assert_reset_asynchronously();
        return check((uint8_t)RESET_VALUE(fast_count_out) == 0
                && (uint8_t)RESET_VALUE(fast_neg_count_out) == 0
                && (uint8_t)RESET_VALUE(slow_count_out) == 0,
            "assertion clears all domains without a clock edge");
    }

    bool test_held_reset_and_release()
    {
        fast_cycle();
        slow_cycle();
        if (!check((uint8_t)RESET_VALUE(fast_count_out) == 0
                && (uint8_t)RESET_VALUE(fast_neg_count_out) == 0
                && (uint8_t)RESET_VALUE(slow_count_out) == 0,
                "clock edges cannot advance state while reset is asserted")) {
            return false;
        }

        release_reset_asynchronously();
        if (!check((uint8_t)RESET_VALUE(fast_count_out) == 0
                && (uint8_t)RESET_VALUE(fast_neg_count_out) == 0
                && (uint8_t)RESET_VALUE(slow_count_out) == 0,
                "asynchronous release does not itself clock state")) {
            return false;
        }

        fast_cycle();
        slow_cycle();
        return check((uint8_t)RESET_VALUE(fast_count_out) == 1
                && (uint8_t)RESET_VALUE(fast_neg_count_out) == 1
                && (uint8_t)RESET_VALUE(slow_count_out) == 1,
            "domains resume on their own clock edges");
    }

public:
    bool run()
    {
#ifndef VERILATOR
        dut.fast_enable_in = _ASSIGN(fast_enable);
        dut.fast_neg_enable_in = _ASSIGN(fast_neg_enable);
        dut.slow_enable_in = _ASSIGN(slow_enable);
        dut._assign();
#else
        dut.fast_clk = 0;
        dut.slow_clk = 0;
        evaluate();
#endif
        return test_generated_async_reset_blocks()
            && test_async_assertion_without_clock_edges()
            && test_held_reset_and_release();
    }
};

#undef RESET_VALUE

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        noveril |= std::strcmp(argv[i], "--noveril") == 0;
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        ok = VerilatorCompileInExactFolder(__FILE__, "AsyncReset", "AsyncReset",
            {"Predef_pkg"}, {"../../../../include"});
        ok = ok && std::system("AsyncReset/obj_dir/VAsyncReset") == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif
    ok = ok && AsyncResetTest().run();
    std::print("Asynchronous reset {}\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
