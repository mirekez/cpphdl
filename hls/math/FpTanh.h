#pragma once
#include <cpphdl.h>
#include <cstddef>
#include <cstdint>

// Combinational floating-point hyperbolic tangent. The core uses the bounded
// [5/4] Pade approximation x*(945+105*x*x+x^4)/(945+420*x*x+15*x^4)
// for |x| < 4 and saturates to one outside that range. The older [3/2]
// approximation had up to 0.026 absolute error, which moved GELU across FP8
// rounding midpoints. All arithmetic is fixed-point and synthesizable.
template<size_t W=32, size_t EW=8>
class FpTanh : public cpphdl::Module
{
    static constexpr size_t MANT_WIDTH = W - EW - 1;
    // At |x| < 4 the largest numerator is below 11524 * 2^(2*FIXED_BITS).
    static constexpr size_t FIXED_BITS = MANT_WIDTH + 4 < 25 ? MANT_WIDTH + 4 : 25;
    static constexpr uint64_t MANT_MASK = (uint64_t(1) << MANT_WIDTH) - 1;
    static constexpr uint64_t EXP_MAX = (uint64_t(1) << EW) - 1;
    static constexpr uint64_t SIGN_MASK = uint64_t(1) << (W - 1);
    static constexpr uint64_t ONE_FIXED = uint64_t(1) << FIXED_BITS;
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
        uint64_t raw;
        uint64_t sign;
        uint64_t exponent;
        uint64_t mantissa;
        uint64_t significand;
        uint64_t x_fixed;
        uint64_t x_squared;
        uint64_t x_fourth;
        uint64_t numerator;
        uint64_t denominator;
        uint64_t y_fixed;
        uint64_t normalized;
        uint64_t discarded;
        uint64_t halfway;
        cpphdl::logic<64> result_raw;
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
        x_fourth = 0;
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

                x_squared = (x_fixed * x_fixed) >> FIXED_BITS;
                x_fourth = (x_squared * x_squared) >> FIXED_BITS;
                numerator = x_fixed * (945 * ONE_FIXED + 105 * x_squared + x_fourth);
                denominator = 945 * ONE_FIXED + 420 * x_squared + 15 * x_fourth;
                y_fixed = (numerator + denominator / 2) / denominator;
                if (y_fixed > ONE_FIXED) {
                    y_fixed = ONE_FIXED;
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

namespace cpphdl::hls::math {
template<size_t W=32, size_t EW=8> using FpTanh = ::FpTanh<W,EW>;
}
