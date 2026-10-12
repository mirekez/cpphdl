#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

#include "../../hls/math/FpSqrt.h"

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include "FpMathTest.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "../tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

template<size_t W, size_t EW>
class TestFpSqrt
{
    using Format = FpMathTestFormat<W, EW>;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    FpSqrt<W, EW> dut;
    logic<W> input_data = 0;
#endif
    bool debug;
    bool error = false;

    uint64_t evaluate(uint64_t raw)
    {
#ifdef VERILATOR
        dut.data_in = raw;
        dut.eval();
        return (uint64_t)dut.data_out & Format::RAW_MASK;
#else
        input_data = (logic<W>)raw;
        ++_system_clock;
        return (uint64_t)dut.data_out() & Format::RAW_MASK;
#endif
    }

    uint64_t expected(uint64_t raw)
    {
        uint64_t exponent;
        uint64_t mantissa;

        exponent = Format::exponent(raw);
        mantissa = Format::mantissa(raw);
        if (exponent == 0) {
            return Format::zero(Format::sign(raw));
        }
        if (exponent == Format::EXP_MASK) {
            if (mantissa == 0 && !Format::sign(raw)) {
                return Format::infinity(false);
            }
            return Format::nan();
        }
        if (Format::sign(raw)) {
            return Format::nan();
        }
        return Format::from_double(std::sqrt(Format::to_double(raw)));
    }

    void check(uint64_t raw)
    {
        uint64_t actual;
        uint64_t reference;

        actual = evaluate(raw);
        reference = expected(raw);
        if (actual != reference) {
            std::printf("FpSqrt<%zu,%zu> ERROR input=%08llx output=%08llx expected=%08llx\n",
                W, EW, (unsigned long long)raw, (unsigned long long)actual,
                (unsigned long long)reference);
            error = true;
        }
        else if (debug) {
            std::printf("FpSqrt<%zu,%zu> input=%08llx output=%08llx\n",
                W, EW, (unsigned long long)raw, (unsigned long long)actual);
        }
    }

public:
    explicit TestFpSqrt(bool debug_in) : debug(debug_in)
    {
#ifndef VERILATOR
        dut.data_in = _ASSIGN_REG(input_data);
        dut._assign();
#endif
    }

    bool run()
    {
        uint64_t raw;
        uint64_t state;
        size_t i;
        auto start = std::chrono::high_resolution_clock::now();

#ifdef VERILATOR
        std::printf("VERILATOR FpSqrt<%zu,%zu>...", W, EW);
#else
        std::printf("C++HDL FpSqrt<%zu,%zu>...", W, EW);
#endif
        check(Format::zero(false));
        check(Format::zero(true));
        check(Format::one(false));
        check(Format::infinity(false));
        check(Format::infinity(true));
        check(Format::nan());

        if constexpr (W <= 16) {
            for (raw = 0; raw <= Format::RAW_MASK && !error; ++raw) {
                check(raw);
            }
        }
        else {
            state = 0x4d595df4d0f33173ULL;
            for (i = 0; i < 100000 && !error; ++i) {
                check(fp_math_random_step(state) & Format::RAW_MASK);
            }
        }

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
    if (!noveril) {
        std::cout << "Building FpSqrt Verilator simulation...\n";
        ok &= VerilatorCompile(__FILE__, "FpSqrt", {"Predef_pkg"}, {"../../../../include"}, 16, 5);
        ok &= VerilatorCompile(__FILE__, "FpSqrt", {"Predef_pkg"}, {"../../../../include"}, 32, 8);
        if (ok) {
            ok &= std::system((std::string("FpSqrt_16_5/obj_dir/VFpSqrt 16 5")
                + (debug ? " --debug" : "")).c_str()) == 0;
            ok &= std::system((std::string("FpSqrt_32_8/obj_dir/VFpSqrt 32 8")
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
            return !(ok && TestFpSqrt<16, 5>(debug).run());
        }
        if (width == 32 && exponent_width == 8) {
            return !(ok && TestFpSqrt<32, 8>(debug).run());
        }
        std::printf("Unsupported FpSqrt format: W=%zu EW=%zu\n", width, exponent_width);
        return 1;
    }

    ok = ok && TestFpSqrt<16, 5>(debug).run();
    ok = ok && TestFpSqrt<32, 8>(debug).run();
    return !ok;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
