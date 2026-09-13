#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

// Combinational floating-point square root. Exponent-zero inputs are flushed
// to signed zero, matching the floating-point policy used by FpConverter.
template<size_t W=32, size_t EW=8>
class FpSqrt : public Module
{
    static constexpr size_t MANT_WIDTH = W - EW - 1;
    static constexpr uint64_t MANT_MASK = (uint64_t(1) << MANT_WIDTH) - 1;
    static constexpr uint64_t EXP_MAX = (uint64_t(1) << EW) - 1;
    static constexpr uint64_t SIGN_MASK = uint64_t(1) << (W - 1);
    static constexpr int EXP_BIAS = (1 << (EW - 1)) - 1;

    static_assert(W >= 4 && W <= 32, "FpSqrt supports widths from 4 to 32 bits");
    static_assert(EW >= 2 && EW < W - 1, "FpSqrt exponent width is invalid");

public:
    _PORT(logic<W>) data_in;
    _PORT(logic<W>) data_out = _ASSIGN_COMB(result_comb_func());

private:
    logic<W> result_comb;

    logic<W>& result_comb_func()
    {
        uint64_t raw;
        uint64_t sign;
        uint64_t exponent;
        uint64_t mantissa;
        uint64_t significand;
        uint64_t radicand;
        uint64_t remainder;
        uint64_t root;
        uint64_t trial;
        uint64_t rounded;
        logic<64> result_raw;
        int exponent_unbiased;
        int result_exponent;
        bool odd_exponent;
        uint8_t i;

        raw = (uint64_t)data_in();
        sign = raw & SIGN_MASK;
        exponent = (raw >> MANT_WIDTH) & EXP_MAX;
        mantissa = raw & MANT_MASK;
        significand = 0;
        radicand = 0;
        remainder = 0;
        root = 0;
        trial = 0;
        rounded = 0;
        result_raw = 0;
        exponent_unbiased = 0;
        result_exponent = 0;
        odd_exponent = false;
        result_comb = 0;

        if (exponent == 0) {
            result_raw = sign;
        }
        else if (exponent == EXP_MAX) {
            if (mantissa == 0 && sign == 0) {
                result_raw = EXP_MAX << MANT_WIDTH;
            }
            else {
                result_raw = (EXP_MAX << MANT_WIDTH) | 1;
            }
        }
        else if (sign != 0) {
            result_raw = (EXP_MAX << MANT_WIDTH) | 1;
        }
        else {
            exponent_unbiased = (int)exponent - EXP_BIAS;
            odd_exponent = (exponent_unbiased % 2) != 0;
            significand = (uint64_t(1) << MANT_WIDTH) | mantissa;
            if (odd_exponent) {
                significand <<= 1;
                --exponent_unbiased;
            }

            // Two extra radicand bits produce one guard bit in root. The
            // restoring loop has a fixed bound and is unrolled by synthesis.
            radicand = significand << (MANT_WIDTH + 2);
            remainder = 0;
            root = 0;
            for (i = 0; i < 32; ++i) {
                remainder = (remainder << 2) | (radicand >> 62);
                radicand <<= 2;
                root <<= 1;
                trial = (root << 1) | 1;
                if (remainder >= trial) {
                    remainder -= trial;
                    root |= 1;
                }
            }

            rounded = root >> 1;
            if ((root & 1) != 0 && (remainder != 0 || (rounded & 1) != 0)) {
                ++rounded;
            }
            result_exponent = exponent_unbiased / 2 + EXP_BIAS;
            if (rounded >= (uint64_t(1) << (MANT_WIDTH + 1))) {
                rounded >>= 1;
                ++result_exponent;
            }
            result_raw = ((uint64_t)result_exponent << MANT_WIDTH)
                | (rounded & MANT_MASK);
        }

        result_comb = result_raw.bits(W - 1, 0);
        return result_comb;
    }

public:
    void _assign() {}
};

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
        std::printf("CppHDL FpSqrt<%zu,%zu>...", W, EW);
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
