#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cpphdl.h>
#include <cstddef>
#include <cstdint>

// Combinational floating-point hyperbolic tangent. A 32-segment linear
// interpolation table covers |x| < 4; larger values saturate to one. The
// Q25 table has at most 0.0015 absolute interpolation error and avoids the
// 64-bit variable divider that could not meet FPGA timing.
template<size_t W=32, size_t EW=8>
class FpTanh : public cpphdl::Module
{
    static constexpr size_t MANT_WIDTH = W - EW - 1;
    static constexpr size_t FIXED_BITS = MANT_WIDTH + 4 < 25 ? MANT_WIDTH + 4 : 25;
    static constexpr uint32_t MANT_MASK = (uint32_t(1) << MANT_WIDTH) - 1;
    static constexpr uint32_t EXP_MAX = (uint32_t(1) << EW) - 1;
    static constexpr uint32_t SIGN_MASK = uint32_t(1) << (W - 1);
    static constexpr uint32_t ONE_FIXED = uint32_t(1) << FIXED_BITS;
    static constexpr int EXP_BIAS = (1 << (EW - 1)) - 1;

    static_assert(W >= 4 && W <= 32, "FpTanh supports widths from 4 to 32 bits");
    static_assert(EW >= 2 && EW < W - 1, "FpTanh exponent width is invalid");

public:
    _PORT(cpphdl::logic<W>) data_in;
    _PORT(cpphdl::logic<W>) data_out = _ASSIGN_COMB(result_comb_func());

private:
    cpphdl::logic<W> result_comb;

    cpphdl::logic<W>& result_comb_func()
    {
        uint32_t raw;
        uint32_t sign;
        uint32_t exponent;
        uint32_t mantissa;
        uint32_t significand;
        uint32_t x_fixed;
        uint32_t fraction;
        uint64_t interpolated;
        uint32_t y_fixed;
        uint32_t y0;
        uint32_t y1;
        uint32_t normalized;
        uint32_t discarded;
        uint32_t halfway;
        cpphdl::logic<32> result_raw;
        int exponent_unbiased;
        int fixed_shift;
        int result_exponent;
        int small_exponent_limit;
        int saturation_exponent_limit;
        int zero_shift;
        uint8_t shift_amount;
        uint8_t msb;
        uint8_t i;
        uint8_t segment;

        raw = (uint32_t)data_in();
        sign = raw & SIGN_MASK;
        exponent = (raw >> MANT_WIDTH) & EXP_MAX;
        mantissa = raw & MANT_MASK;
        significand = 0;
        x_fixed = 0;
        fraction = 0;
        interpolated = 0;
        y_fixed = 0;
        y0 = 0;
        y1 = 0;
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
        segment = 0;
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

                segment = (uint8_t)(x_fixed >> (FIXED_BITS - 3));
                if (segment >= 32) {
                    y_fixed = ONE_FIXED;
                }
                else {
                    fraction = x_fixed & ((uint64_t(1) << (FIXED_BITS - 3)) - 1);
                    switch (segment) {
                    case 0: y0 = 0u; y1 = 4172594u; break;
                    case 1: y0 = 4172594u; y1 = 8218107u; break;
                    case 2: y0 = 8218107u; y1 = 12024479u; break;
                    case 3: y0 = 12024479u; y1 = 15506079u; break;
                    case 4: y0 = 15506079u; y1 = 18609279u; break;
                    case 5: y0 = 18609279u; y1 = 21312062u; break;
                    case 6: y0 = 21312062u; y1 = 23619153u; break;
                    case 7: y0 = 23619153u; y1 = 25554859u; break;
                    case 8: y0 = 25554859u; y1 = 27155638u; break;
                    case 9: y0 = 27155638u; y1 = 28463676u; break;
                    case 10: y0 = 28463676u; y1 = 29522085u; break;
                    case 11: y0 = 29522085u; y1 = 30371736u; break;
                    case 12: y0 = 30371736u; y1 = 31049467u; break;
                    case 13: y0 = 31049467u; y1 = 31587321u; break;
                    case 14: y0 = 31587321u; y1 = 32012447u; break;
                    case 15: y0 = 32012447u; y1 = 32347398u; break;
                    case 16: y0 = 32347398u; y1 = 32610638u; break;
                    case 17: y0 = 32610638u; y1 = 32817111u; break;
                    case 18: y0 = 32817111u; y1 = 32978807u; break;
                    case 19: y0 = 32978807u; y1 = 33105282u; break;
                    case 20: y0 = 33105282u; y1 = 33204115u; break;
                    case 21: y0 = 33204115u; y1 = 33281290u; break;
                    case 22: y0 = 33281290u; y1 = 33341517u; break;
                    case 23: y0 = 33341517u; y1 = 33388497u; break;
                    case 24: y0 = 33388497u; y1 = 33425131u; break;
                    case 25: y0 = 33425131u; y1 = 33453689u; break;
                    case 26: y0 = 33453689u; y1 = 33475947u; break;
                    case 27: y0 = 33475947u; y1 = 33493292u; break;
                    case 28: y0 = 33493292u; y1 = 33506807u; break;
                    case 29: y0 = 33506807u; y1 = 33517336u; break;
                    case 30: y0 = 33517336u; y1 = 33525538u; break;
                    case 31: y0 = 33525538u; y1 = 33531927u; break;
                    default: y0 = 0; y1 = 0; break;
                    }
                    interpolated = y0 + (((uint64_t)(y1 - y0) * fraction
                        + (uint64_t(1) << (FIXED_BITS - 4))) >> (FIXED_BITS - 3));
                    y_fixed = interpolated >> (25 - FIXED_BITS);
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
#pragma pop_macro("MAINFILE")

#if !defined(SYNTHESIS) && defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cpphdl.h>

using namespace cpphdl;



#include "../../lib/math/FpMathTest.h"
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

int main_from_header(int argc, char** argv)
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
#pragma pop_macro("MAINFILE")
#endif
