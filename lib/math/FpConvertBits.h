#pragma once
#include <cstdint>
#include <type_traits>

namespace cpphdl_examples::math {
// Shared raw-bit implementation: no FP object storage is needed by HLS.
struct FpFormatConversion {
    static uint64_t convert(uint64_t value, unsigned width, unsigned exp_width,
                            unsigned out_width, unsigned out_exp_width,
                            bool preserve_subnormals, bool preserve_nan_payload)
    {
        using Work = uint64_t;
        unsigned mant_width, out_mant_width;
        uint64_t exp_max, mant_max, out_exp_max, out_mant_max;
        int exp_bias, out_exp_bias;
        Work significand, retained, tail, halfway, leading;
        uint64_t output_sign, payload;
        unsigned input_exponent, normalize;
        int output_exponent, shift;

        mant_width = width - exp_width - 1;
        out_mant_width = out_width - out_exp_width - 1;
        exp_max = (uint64_t(1) << exp_width) - 1;
        mant_max = (uint64_t(1) << mant_width) - 1;
        out_exp_max = (uint64_t(1) << out_exp_width) - 1;
        out_mant_max = (uint64_t(1) << out_mant_width) - 1;
        exp_bias = (1 << (exp_width - 1)) - 1;
        out_exp_bias = (1 << (out_exp_width - 1)) - 1;
        output_sign = ((uint64_t(value) >> (width - 1)) & 1) << (out_width - 1);
        input_exponent = unsigned((uint64_t(value) >> mant_width) & exp_max);
        significand = Work(uint64_t(value) & mant_max);
        if (input_exponent == exp_max) {
            payload = significand ? 1 : 0;
            if (preserve_nan_payload && significand) {
                if (out_mant_width >= mant_width)
                    payload = uint64_t(significand) << (out_mant_width - mant_width);
                else
                    payload = uint64_t(significand) >> (mant_width - out_mant_width);
                if (!payload)
                    payload = 1; // A truncated NaN must never become infinity.
            }
            return uint64_t(output_sign | (out_exp_max << out_mant_width) | payload);
        }
        if (!input_exponent && (!significand || !preserve_subnormals)) {
            return uint64_t(output_sign);
        }

        output_exponent = int(input_exponent ? input_exponent : 1) - exp_bias + out_exp_bias;
        if (input_exponent) {
            significand |= Work(1) << mant_width;
        } else {
            leading = significand;
            leading |= leading >> 1;
            leading |= leading >> 2;
            leading |= leading >> 4;
            leading |= leading >> 8;
            leading |= leading >> 16;
            if constexpr (sizeof(Work) > 4) leading |= leading >> 32;
            leading ^= leading >> 1;
            normalize = mant_width - ((unsigned((leading & 0xffffffff00000000ULL) != 0) << 5) |
                (unsigned((leading & 0xffff0000ffff0000ULL) != 0) << 4) |
                (unsigned((leading & 0xff00ff00ff00ff00ULL) != 0) << 3) |
                (unsigned((leading & 0xf0f0f0f0f0f0f0f0ULL) != 0) << 2) |
                (unsigned((leading & 0xccccccccccccccccULL) != 0) << 1) |
                unsigned((leading & 0xaaaaaaaaaaaaaaaaULL) != 0));
            significand <<= normalize;
            output_exponent -= int(normalize);
        }
        if (output_exponent <= 0 && !preserve_subnormals) {
            return uint64_t(output_sign);
        }
        if (output_exponent >= int(out_exp_max)) {
            return uint64_t(output_sign | (out_exp_max << out_mant_width));
        }

        shift = int(mant_width) - int(out_mant_width);
        if (output_exponent <= 0)
            shift += 1 - output_exponent;
        retained = 0;
        if (shift <= 0) {
            retained = significand << unsigned(-shift);
        } else if (shift < int(sizeof(Work) * 8)) {
            retained = significand >> unsigned(shift);
            tail = significand & ((Work(1) << unsigned(shift)) - 1);
            halfway = Work(1) << unsigned(shift - 1);
            if (tail > halfway || (tail == halfway && (retained & 1)))
                ++retained;
        }
        // Work has at least two spare bits above the source mantissa, so a
        // right shift by its width or more always rounds to zero.
        if (output_exponent <= 0) {
            output_exponent = retained >= (Work(1) << out_mant_width) ? 1 : 0;
        } else if (retained >= (Work(1) << (out_mant_width + 1))) {
            retained >>= 1;
            ++output_exponent;
        }
        if (output_exponent >= int(out_exp_max)) {
            return uint64_t(output_sign | (out_exp_max << out_mant_width));
        }
        return uint64_t(output_sign | (uint64_t(output_exponent) << out_mant_width)
                                  | (uint64_t(retained) & out_mant_max));
    }

};

// Raw integer conversion is also usable without the RTL FP wrapper types.
template<unsigned W, unsigned EW>
struct IntFormatConversion {
    using RAW = std::conditional_t<(W <= 32), uint32_t, uint64_t>;
    static constexpr unsigned WIDTH = W, MANT_WIDTH = W - EW - 1;
    static constexpr unsigned EXP_BIAS = (1u << (EW - 1)) - 1;
    static constexpr uint64_t EXP_MAX = (uint64_t(1) << EW) - 1;
    static constexpr uint64_t MANT_MAX = (uint64_t(1) << MANT_WIDTH) - 1;
    // Signed int32 input, round to nearest/even, with overflow to infinity.
    static RAW from_int_raw(int32_t value)
    {
        using Work = std::conditional_t<(MANT_WIDTH <= 23), uint32_t, uint64_t>;
        uint32_t magnitude, leading;
        Work retained, tail, halfway;
        unsigned msb, exponent_work, shift;
        uint64_t output_sign;
        if (!value)
            return RAW(0);
        output_sign = value < 0 ? uint64_t(1) << (WIDTH - 1) : 0;
        magnitude = value < 0 ? 0u - uint32_t(value) : uint32_t(value);
        leading = magnitude;
        leading |= leading >> 1;
        leading |= leading >> 2;
        leading |= leading >> 4;
        leading |= leading >> 8;
        leading |= leading >> 16;
        leading ^= leading >> 1;
        msb = (unsigned((leading & 0xffff0000u) != 0) << 4) |
              (unsigned((leading & 0xff00ff00u) != 0) << 3) |
              (unsigned((leading & 0xf0f0f0f0u) != 0) << 2) |
              (unsigned((leading & 0xccccccccu) != 0) << 1) |
              unsigned((leading & 0xaaaaaaaau) != 0);
        exponent_work = msb + EXP_BIAS;
        if (msb <= MANT_WIDTH) {
            retained = Work(magnitude) << (MANT_WIDTH - msb);
        } else {
            shift = msb - MANT_WIDTH;
            retained = Work(magnitude) >> shift;
            tail = Work(magnitude) & ((Work(1) << shift) - 1);
            halfway = Work(1) << (shift - 1);
            if (tail > halfway || (tail == halfway && (retained & 1)))
                ++retained;
        }
        if (retained >= (Work(1) << (MANT_WIDTH + 1))) {
            retained >>= 1;
            ++exponent_work;
        }
        if (exponent_work >= EXP_MAX)
            return RAW(output_sign | (EXP_MAX << MANT_WIDTH));
        return RAW(output_sign | (uint64_t(exponent_work) << MANT_WIDTH)
                   | (uint64_t(retained) & MANT_MAX));
    }
};
} // namespace cpphdl_examples::math
