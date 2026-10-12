#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

#include "../../hls/math/FpAdd.h"

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include "FpMathTest.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "../tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static bool generated_sv_has_registered_sum()
{
    std::ifstream input("generated/FpAdd.sv");
    std::string text;

    if (!input) {
        std::printf("can't open generated/FpAdd.sv\n");
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return text.find("input wire sum_clock") != std::string::npos
        && text.find("input wire en_in") != std::string::npos
        && text.find("always @(posedge sum_clock)") != std::string::npos
        && text.find("always_comb begin : add_lhs_comb_func") != std::string::npos
        && text.find("always_comb begin : add_rhs_comb_func") != std::string::npos
        && text.find("prepare_comb_func") == std::string::npos
        && text.find("if (en_in) begin") != std::string::npos
        && text.find("add_lhs_comb + add_rhs_comb") != std::string::npos;
}

template<size_t W, size_t EW>
class TestFpAdd
{
    using Format = FpMathTestFormat<W, EW>;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    FpAdd<W, EW> dut;
    logic<W> a_data = 0;
    logic<W> b_data = 0;
    bool input_enable = true;
#endif
    bool debug;
    bool error = false;
    uint64_t expected_data = 0;

    static double operand_value(uint64_t raw)
    {
        if (Format::exponent(raw) == 0) {
            return Format::sign(raw) ? -0.0 : 0.0;
        }
        return Format::to_double(raw);
    }

    static uint64_t reference(uint64_t a, uint64_t b)
    {
        return Format::from_double(operand_value(a) + operand_value(b));
    }

    void set_inputs(uint64_t a, uint64_t b, bool enable, bool reset)
    {
#ifdef VERILATOR
        dut.a_in = a;
        dut.b_in = b;
        dut.en_in = enable;
        dut.reset = reset;
#else
        a_data = (logic<W>)a;
        b_data = (logic<W>)b;
        input_enable = enable;
#endif
    }

    uint64_t output_data()
    {
#ifdef VERILATOR
        return (uint64_t)dut.c_out & Format::RAW_MASK;
#else
        return (uint64_t)dut.c_out() & Format::RAW_MASK;
#endif
    }

    void check_output(const char* phase, uint64_t a, uint64_t b)
    {
        uint64_t actual;

        actual = output_data();
        if (actual != expected_data) {
            std::printf("FpAdd<%zu,%zu> ERROR %s a=%08llx b=%08llx output=%08llx expected=%08llx\n",
                W, EW, phase, (unsigned long long)a, (unsigned long long)b,
                (unsigned long long)actual, (unsigned long long)expected_data);
            error = true;
        }
        else if (debug) {
            std::printf("FpAdd<%zu,%zu> a=%08llx b=%08llx output=%08llx\n",
                W, EW, (unsigned long long)a, (unsigned long long)b,
                (unsigned long long)actual);
        }
    }

    void cycle(uint64_t a, uint64_t b, bool reset = false, bool enable = true)
    {
        set_inputs(a, b, enable, reset);
#ifdef VERILATOR
        dut.sum_clock = 0;
        dut.eval();
#endif
        check_output("before edge", a, b);

#ifdef VERILATOR
        dut.sum_clock = 1;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
#endif
        ++_system_clock;
        if (reset) {
            expected_data = 0;
        }
        else if (enable) {
            expected_data = reference(a, b);
        }
        check_output("after edge", a, b);

#ifdef VERILATOR
        dut.sum_clock = 0;
        dut.eval();
#endif
    }

    void directed_tests()
    {
        cycle(Format::zero(false), Format::zero(false));
        cycle(Format::zero(true), Format::zero(true));
        cycle(Format::one(false), Format::one(false));
        cycle(Format::one(false), Format::one(true));
        cycle(Format::infinity(false), Format::one(false));
        cycle(Format::infinity(true), Format::one(false));
        cycle(Format::infinity(false), Format::infinity(true));
        cycle(Format::nan(), Format::one(false));
        cycle(Format::from_double(1.5), Format::from_double(2.25));
        cycle(Format::one(false), Format::one(false), false, false);
        cycle(Format::from_double(65504.0), Format::from_double(65504.0));
        cycle(Format::from_double(1.0), Format::from_double(0.0009765625));
        cycle(0, 0);
    }

public:
    explicit TestFpAdd(bool debug_in) : debug(debug_in)
    {
#ifndef VERILATOR
        dut.a_in = _ASSIGN(a_data);
        dut.b_in = _ASSIGN(b_data);
        dut.en_in = _ASSIGN(input_enable);
        dut._assign();
#else
        dut.sum_clock = 0;
#endif
    }

    bool run()
    {
        uint64_t a;
        uint64_t b;
        uint64_t state;
        size_t i;
        size_t iterations;
        auto start = std::chrono::high_resolution_clock::now();

#ifdef VERILATOR
        std::printf("VERILATOR FpAdd<%zu,%zu>...", W, EW);
#else
        std::printf("C++HDL FpAdd<%zu,%zu>...", W, EW);
#endif
        cycle(0, 0, true);
        cycle(0, 0, true);
        directed_tests();

        state = 0x2d99787926d46932ULL;
        iterations = W <= 16 ? 65536 : 100000;
        for (i = 0; i < iterations && !error; ++i) {
            a = W <= 16 ? i : fp_math_random_step(state);
            b = fp_math_random_step(state);
            cycle(a & Format::RAW_MASK, b & Format::RAW_MASK);
        }
        cycle(0, 0);

        std::printf(" %s (%lld microseconds)\n", error ? "FAILED" : "PASSED",
            (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    bool ok = true;
    std::vector<std::string> positional;
    size_t i;

    for (i = 1; i < (size_t)argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        else if (std::strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        else {
            positional.emplace_back(argv[i]);
        }
    }

#ifndef VERILATOR
    ok = generated_sv_has_registered_sum();
    if (!noveril && ok) {
        std::cout << "Building FpAdd Verilator simulation...\n";
        ok &= VerilatorCompile(__FILE__, "FpAdd", {"Predef_pkg"}, {"../../../../include"}, 16, 5);
        ok &= VerilatorCompile(__FILE__, "FpAdd", {"Predef_pkg"}, {"../../../../include"}, 32, 8);
        if (ok) {
            ok &= std::system((std::string("FpAdd_16_5/obj_dir/VFpAdd 16 5")
                + (debug ? " --debug" : "")).c_str()) == 0;
            ok &= std::system((std::string("FpAdd_32_8/obj_dir/VFpAdd 32 8")
                + (debug ? " --debug" : "")).c_str()) == 0;
        }
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    if (positional.size() >= 2) {
        size_t width = std::stoull(positional[0]);
        size_t exponent_width = std::stoull(positional[1]);
        if (width == 16 && exponent_width == 5) {
            return !(ok && TestFpAdd<16, 5>(debug).run());
        }
        if (width == 32 && exponent_width == 8) {
            return !(ok && TestFpAdd<32, 8>(debug).run());
        }
        std::printf("Unsupported FpAdd format: W=%zu EW=%zu\n", width, exponent_width);
        return 1;
    }

    ok = ok && TestFpAdd<16, 5>(debug).run();
    ok = ok && TestFpAdd<32, 8>(debug).run();
    return !ok;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
