#pragma once
#include <cpphdl.h>
#include <cstddef>
#include <cstdint>

// Combinational floating-point square root. Exponent-zero inputs are flushed
// to signed zero, matching the floating-point policy used by FpConverter.
template<size_t W=32, size_t EW=8>
class FpSqrt : public cpphdl::Module
{
    static constexpr size_t MANT_WIDTH = W - EW - 1;
    static constexpr uint64_t MANT_MASK = (uint64_t(1) << MANT_WIDTH) - 1;
    static constexpr uint64_t EXP_MAX = (uint64_t(1) << EW) - 1;
    static constexpr uint64_t SIGN_MASK = uint64_t(1) << (W - 1);
    static constexpr int EXP_BIAS = (1 << (EW - 1)) - 1;

    static_assert(W >= 4 && W <= 32, "FpSqrt supports widths from 4 to 32 bits");
    static_assert(EW >= 2 && EW < W - 1, "FpSqrt exponent width is invalid");

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
        uint64_t radicand;
        uint64_t remainder;
        uint64_t root;
        uint64_t trial;
        uint64_t rounded;
        cpphdl::logic<64> result_raw;
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

namespace cpphdl::hls::math {
template<size_t W=32, size_t EW=8> using FpSqrt = ::FpSqrt<W,EW>;
}
