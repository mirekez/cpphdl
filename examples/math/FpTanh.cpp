#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

// Combinational floating-point hyperbolic tangent. The core uses the bounded
// rational approximation x*(27+x*x)/(27+9*x*x) for |x| < 3 and saturates to
// one outside that range. All arithmetic is fixed-point and synthesizable.
template<size_t W=32, size_t EW=8>
class FpTanh : public Module
{
    static constexpr size_t MANT_WIDTH = W - EW - 1;
    static constexpr size_t FIXED_BITS = MANT_WIDTH + 4 < 26 ? MANT_WIDTH + 4 : 26;
    static constexpr uint64_t MANT_MASK = (uint64_t(1) << MANT_WIDTH) - 1;
    static constexpr uint64_t EXP_MAX = (uint64_t(1) << EW) - 1;
    static constexpr uint64_t SIGN_MASK = uint64_t(1) << (W - 1);
    static constexpr uint64_t ONE_FIXED = uint64_t(1) << FIXED_BITS;
    static constexpr int EXP_BIAS = (1 << (EW - 1)) - 1;

    static_assert(W >= 4 && W <= 32, "FpTanh supports widths from 4 to 32 bits");
    static_assert(EW >= 2 && EW < W - 1, "FpTanh exponent width is invalid");

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
        uint64_t x_fixed;
        uint64_t x_squared;
        uint64_t numerator;
        uint64_t denominator;
        uint64_t y_fixed;
        uint64_t normalized;
        uint64_t discarded;
        uint64_t halfway;
        logic<64> result_raw;
        int exponent_unbiased;
        int fixed_shift;
        int result_exponent;
        int small_exponent_limit;
        int saturation_exponent_limit;
        int zero_shift;
        uint8_t shift_amount;
        uint8_t msb;
        uint8_t i;

        raw = (uint64_t)data_in();
        sign = raw & SIGN_MASK;
        exponent = (raw >> MANT_WIDTH) & EXP_MAX;
        mantissa = raw & MANT_MASK;
        significand = 0;
        x_fixed = 0;
        x_squared = 0;
        numerator = 0;
        denominator = 1;
        y_fixed = 0;
        normalized = 0;
        discarded = 0;
        halfway = 0;
        result_raw = 0;
        exponent_unbiased = 0;
        fixed_shift = 0;
        result_exponent = 0;
        small_exponent_limit = -5;
        saturation_exponent_limit = 2;
        zero_shift = 0;
        shift_amount = 0;
        msb = 0;
        result_comb = 0;

        if (exponent == 0) {
            result_raw = sign;
        }
        else if (exponent == EXP_MAX) {
            if (mantissa != 0) {
                result_raw = (EXP_MAX << MANT_WIDTH) | 1;
            }
            else {
                result_raw = sign | ((uint64_t)EXP_BIAS << MANT_WIDTH);
            }
        }
        else {
            exponent_unbiased = (int)exponent - EXP_BIAS;
            if (exponent_unbiased <= small_exponent_limit) {
                // tanh(x) differs from x by less than 1.1e-5 in this range.
                // Passing the encoded value through also preserves tiny normals.
                result_raw = raw;
            }
            else if (exponent_unbiased >= saturation_exponent_limit) {
                result_raw = sign | ((uint64_t)EXP_BIAS << MANT_WIDTH);
            }
            else {
                significand = (uint64_t(1) << MANT_WIDTH) | mantissa;
                fixed_shift = (int)FIXED_BITS + exponent_unbiased - (int)MANT_WIDTH;
                if (fixed_shift >= zero_shift) {
                    x_fixed = significand << fixed_shift;
                }
                else {
                    shift_amount = (uint8_t)(-fixed_shift);
                    x_fixed = significand >> shift_amount;
                    discarded = significand & ((uint64_t(1) << shift_amount) - 1);
                    halfway = uint64_t(1) << (shift_amount - 1);
                    if (discarded > halfway || (discarded == halfway && (x_fixed & 1) != 0)) {
                        ++x_fixed;
                    }
                }

                if (x_fixed >= 3 * ONE_FIXED) {
                    y_fixed = ONE_FIXED;
                }
                else {
                    x_squared = (x_fixed * x_fixed) >> FIXED_BITS;
                    numerator = x_fixed * (27 * ONE_FIXED + x_squared);
                    denominator = 27 * ONE_FIXED + 9 * x_squared;
                    y_fixed = (numerator + denominator / 2) / denominator;
                    if (y_fixed > ONE_FIXED) {
                        y_fixed = ONE_FIXED;
                    }
                }

                if (y_fixed == 0) {
                    result_raw = sign;
                }
                else if (y_fixed >= ONE_FIXED) {
                    result_raw = sign | ((uint64_t)EXP_BIAS << MANT_WIDTH);
                }
                else {
                    msb = 0;
                    for (i = 0; i < 32; ++i) {
                        if ((y_fixed >> i) != 0) {
                            msb = i;
                        }
                    }
                    result_exponent = (int)msb - (int)FIXED_BITS + EXP_BIAS;
                    if (result_exponent <= zero_shift) {
                        result_raw = sign;
                    }
                    else {
                        if (msb > MANT_WIDTH) {
                            shift_amount = msb - MANT_WIDTH;
                            normalized = y_fixed >> shift_amount;
                            discarded = y_fixed & ((uint64_t(1) << shift_amount) - 1);
                            halfway = uint64_t(1) << (shift_amount - 1);
                            if (discarded > halfway || (discarded == halfway && (normalized & 1) != 0)) {
                                ++normalized;
                            }
                        }
                        else {
                            normalized = y_fixed << (MANT_WIDTH - msb);
                        }
                        if (normalized >= (uint64_t(1) << (MANT_WIDTH + 1))) {
                            normalized >>= 1;
                            ++result_exponent;
                        }
                        result_raw = sign | ((uint64_t)result_exponent << MANT_WIDTH)
                            | (normalized & MANT_MASK);
                    }
                }
            }
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
class TestFpTanh
{
    using Format = FpMathTestFormat<W, EW>;
    static constexpr double MAX_ABS_ERROR = 0.026;

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
        std::printf("CppHDL FpTanh<%zu,%zu>...", W, EW);
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
