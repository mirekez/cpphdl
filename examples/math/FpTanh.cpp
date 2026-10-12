#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

#include "../../hls/math/FpTanh.h"

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
class TestFpTanh
{
    using Format = FpMathTestFormat<W, EW>;
    static constexpr double MAX_ABS_ERROR = 0.0025;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    FpTanh<W, EW> dut;
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

    void check(uint64_t raw)
    {
        uint64_t actual;
        uint64_t exponent;
        uint64_t mantissa;
        double input;
        double output;
        double reference;
        bool valid;

        actual = evaluate(raw);
        exponent = Format::exponent(raw);
        mantissa = Format::mantissa(raw);
        valid = true;
        if (exponent == 0) {
            valid = actual == Format::zero(Format::sign(raw));
        }
        else if (exponent == Format::EXP_MASK) {
            if (mantissa != 0) {
                valid = actual == Format::nan();
            }
            else {
                valid = actual == Format::one(Format::sign(raw));
            }
        }
        else {
            input = Format::to_double(raw);
            output = Format::to_double(actual);
            reference = std::tanh(input);
            valid = std::isfinite(output)
                && std::fabs(output - reference) <= MAX_ABS_ERROR
                && std::fabs(output) <= 1.0;
        }

        if (!valid) {
            std::printf("FpTanh<%zu,%zu> ERROR input=%08llx (%g) output=%08llx (%g) reference=%g\n",
                W, EW, (unsigned long long)raw, Format::to_double(raw),
                (unsigned long long)actual, Format::to_double(actual),
                std::tanh(Format::to_double(raw)));
            error = true;
        }
        else if (debug) {
            std::printf("FpTanh<%zu,%zu> input=%08llx output=%08llx\n",
                W, EW, (unsigned long long)raw, (unsigned long long)actual);
        }
    }

public:
    explicit TestFpTanh(bool debug_in) : debug(debug_in)
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
        int point;
        auto start = std::chrono::high_resolution_clock::now();

#ifdef VERILATOR
        std::printf("VERILATOR FpTanh<%zu,%zu>...", W, EW);
#else
        std::printf("C++HDL FpTanh<%zu,%zu>...", W, EW);
#endif
        check(Format::zero(false));
        check(Format::zero(true));
        check(Format::one(false));
        check(Format::one(true));
        check(Format::infinity(false));
        check(Format::infinity(true));
        check(Format::nan());

        if constexpr (W <= 16) {
            for (raw = 0; raw <= Format::RAW_MASK && !error; ++raw) {
                check(raw);
            }
        }
        else {
            state = 0x8f3f73b5cf1c9adeULL;
            for (i = 0; i < 100000 && !error; ++i) {
                check(fp_math_random_step(state) & Format::RAW_MASK);
            }
            for (point = -12000; point <= 12000 && !error; ++point) {
                check(Format::from_double((double)point / 4000.0));
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
        std::cout << "Building FpTanh Verilator simulation...\n";
        ok &= VerilatorCompile(__FILE__, "FpTanh", {"Predef_pkg"}, {"../../../../include"}, 16, 5);
        ok &= VerilatorCompile(__FILE__, "FpTanh", {"Predef_pkg"}, {"../../../../include"}, 32, 8);
        if (ok) {
            ok &= std::system((std::string("FpTanh_16_5/obj_dir/VFpTanh 16 5")
                + (debug ? " --debug" : "")).c_str()) == 0;
            ok &= std::system((std::string("FpTanh_32_8/obj_dir/VFpTanh 32 8")
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
            return !(ok && TestFpTanh<16, 5>(debug).run());
        }
        if (width == 32 && exponent_width == 8) {
            return !(ok && TestFpTanh<32, 8>(debug).run());
        }
        std::printf("Unsupported FpTanh format: W=%zu EW=%zu\n", width, exponent_width);
        return 1;
    }

    ok = ok && TestFpTanh<16, 5>(debug).run();
    ok = ok && TestFpTanh<32, 8>(debug).run();
    return !ok;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
