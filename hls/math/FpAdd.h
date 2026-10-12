#pragma once
#include <cpphdl.h>
#include <cstddef>
#include <cstdint>

// One-stage floating-point adder. Alignment is combinational before sum_clock,
// the signed significand addition is registered, and normalization/rounding is
// combinational after the register. Exponent-zero values are flushed to zero.
template<size_t W=32, size_t EW=8>
class FpAdd : public cpphdl::Module
{
    static constexpr size_t MANT_WIDTH = W - EW - 1;
    static constexpr size_t SUM_WIDTH = ((MANT_WIDTH + 6 + 7) / 8) * 8;
    static constexpr uint64_t MANT_MASK = (uint64_t(1) << MANT_WIDTH) - 1;
    static constexpr uint64_t EXP_MAX = (uint64_t(1) << EW) - 1;
    static constexpr uint64_t SIGN_MASK = uint64_t(1) << (W - 1);
    static constexpr uint64_t SUM_MASK = (uint64_t(1) << SUM_WIDTH) - 1;

    static_assert(W >= 4 && W <= 32, "FpAdd supports widths from 4 to 32 bits");
    static_assert(EW >= 2 && EW < W - 1, "FpAdd exponent width is invalid");

public:
    _PORT(cpphdl::logic<W>) a_in;
    _PORT(cpphdl::logic<W>) b_in;
    _PORT(bool) en_in;
    _PORT(cpphdl::logic<W>) c_out = _ASSIGN_COMB(result_comb_func());

private:
    cpphdl::reg<cpphdl::logic<SUM_WIDTH>> sum_reg;
    cpphdl::reg<cpphdl::u<EW>> exponent_reg;
    cpphdl::reg<cpphdl::logic<W>> special_result_reg;
    cpphdl::reg<cpphdl::u1> special_reg;

    cpphdl::logic<SUM_WIDTH> add_lhs_comb;
    cpphdl::logic<SUM_WIDTH> add_rhs_comb;
    cpphdl::u<EW> exponent_comb;
    cpphdl::logic<W> special_result_comb;
    bool special_comb;

    cpphdl::logic<SUM_WIDTH>& add_lhs_comb_func()
    {
        uint64_t raw_a;
        uint64_t raw_b;
        uint64_t exponent_a;
        uint64_t exponent_b;
        uint64_t mantissa_a;
        uint64_t significand_a;
        uint64_t aligned_a;
        uint64_t discarded;
        uint64_t shift_mask;
        cpphdl::logic<64> encoded;
        uint32_t shift;
        bool sign_a;
        bool special;

        raw_a = (uint64_t)a_in();
        raw_b = (uint64_t)b_in();
        exponent_a = (raw_a >> MANT_WIDTH) & EXP_MAX;
        exponent_b = (raw_b >> MANT_WIDTH) & EXP_MAX;
        mantissa_a = raw_a & MANT_MASK;
        sign_a = (raw_a & SIGN_MASK) != 0;
        special = exponent_a == 0 || exponent_b == 0
            || exponent_a == EXP_MAX || exponent_b == EXP_MAX;
        significand_a = 0;
        aligned_a = 0;
        discarded = 0;
        shift_mask = 0;
        encoded = 0;
        shift = 0;
        add_lhs_comb = 0;

        if (!special) {
            significand_a = ((uint64_t(1) << MANT_WIDTH) | mantissa_a) << 3;
            aligned_a = significand_a;
            if (exponent_a < exponent_b) {
                shift = (uint32_t)(exponent_b - exponent_a);
                if (shift >= SUM_WIDTH) {
                    aligned_a = significand_a != 0 ? 1 : 0;
                }
                else if (shift != 0) {
                    shift_mask = (uint64_t(1) << shift) - 1;
                    discarded = significand_a & shift_mask;
                    aligned_a = significand_a >> shift;
                    if (discarded != 0) {
                        aligned_a |= 1;
                    }
                }
            }
            encoded = sign_a
                ? ((uint64_t(0) - aligned_a) & SUM_MASK) : aligned_a;
            add_lhs_comb = encoded.bits(SUM_WIDTH - 1, 0);
        }
        return add_lhs_comb;
    }

    cpphdl::logic<SUM_WIDTH>& add_rhs_comb_func()
    {
        uint64_t raw_a;
        uint64_t raw_b;
        uint64_t exponent_a;
        uint64_t exponent_b;
        uint64_t mantissa_b;
        uint64_t significand_b;
        uint64_t aligned_b;
        uint64_t discarded;
        uint64_t shift_mask;
        cpphdl::logic<64> encoded;
        uint32_t shift;
        bool sign_b;
        bool special;

        raw_a = (uint64_t)a_in();
        raw_b = (uint64_t)b_in();
        exponent_a = (raw_a >> MANT_WIDTH) & EXP_MAX;
        exponent_b = (raw_b >> MANT_WIDTH) & EXP_MAX;
        mantissa_b = raw_b & MANT_MASK;
        sign_b = (raw_b & SIGN_MASK) != 0;
        special = exponent_a == 0 || exponent_b == 0
            || exponent_a == EXP_MAX || exponent_b == EXP_MAX;
        significand_b = 0;
        aligned_b = 0;
        discarded = 0;
        shift_mask = 0;
        encoded = 0;
        shift = 0;
        add_rhs_comb = 0;

        if (!special) {
            significand_b = ((uint64_t(1) << MANT_WIDTH) | mantissa_b) << 3;
            aligned_b = significand_b;
            if (exponent_b < exponent_a) {
                shift = (uint32_t)(exponent_a - exponent_b);
                if (shift >= SUM_WIDTH) {
                    aligned_b = significand_b != 0 ? 1 : 0;
                }
                else if (shift != 0) {
                    shift_mask = (uint64_t(1) << shift) - 1;
                    discarded = significand_b & shift_mask;
                    aligned_b = significand_b >> shift;
                    if (discarded != 0) {
                        aligned_b |= 1;
                    }
                }
            }
            encoded = sign_b
                ? ((uint64_t(0) - aligned_b) & SUM_MASK) : aligned_b;
            add_rhs_comb = encoded.bits(SUM_WIDTH - 1, 0);
        }
        return add_rhs_comb;
    }

    cpphdl::u<EW>& exponent_comb_func()
    {
        uint64_t exponent_a;
        uint64_t exponent_b;
        cpphdl::logic<64> exponent_wide;

        exponent_a = ((uint64_t)a_in() >> MANT_WIDTH) & EXP_MAX;
        exponent_b = ((uint64_t)b_in() >> MANT_WIDTH) & EXP_MAX;
        exponent_wide = 0;
        exponent_comb = 0;
        if (exponent_a != 0 && exponent_b != 0
            && exponent_a != EXP_MAX && exponent_b != EXP_MAX) {
            exponent_wide = exponent_a >= exponent_b ? exponent_a : exponent_b;
            exponent_comb = exponent_wide.bits(EW - 1, 0);
        }
        return exponent_comb;
    }

    bool& special_comb_func()
    {
        uint64_t exponent_a;
        uint64_t exponent_b;

        exponent_a = ((uint64_t)a_in() >> MANT_WIDTH) & EXP_MAX;
        exponent_b = ((uint64_t)b_in() >> MANT_WIDTH) & EXP_MAX;
        special_comb = exponent_a == 0 || exponent_b == 0
            || exponent_a == EXP_MAX || exponent_b == EXP_MAX;
        return special_comb;
    }

    cpphdl::logic<W>& special_result_comb_func()
    {
        uint64_t raw_a;
        uint64_t raw_b;
        uint64_t exponent_a;
        uint64_t exponent_b;
        uint64_t mantissa_a;
        uint64_t mantissa_b;
        cpphdl::logic<64> result_wide;
        bool sign_a;
        bool sign_b;
        bool zero_a;
        bool zero_b;
        bool infinity_a;
        bool infinity_b;
        bool nan_a;
        bool nan_b;

        raw_a = (uint64_t)a_in();
        raw_b = (uint64_t)b_in();
        exponent_a = (raw_a >> MANT_WIDTH) & EXP_MAX;
        exponent_b = (raw_b >> MANT_WIDTH) & EXP_MAX;
        mantissa_a = raw_a & MANT_MASK;
        mantissa_b = raw_b & MANT_MASK;
        sign_a = (raw_a & SIGN_MASK) != 0;
        sign_b = (raw_b & SIGN_MASK) != 0;
        zero_a = exponent_a == 0;
        zero_b = exponent_b == 0;
        infinity_a = exponent_a == EXP_MAX && mantissa_a == 0;
        infinity_b = exponent_b == EXP_MAX && mantissa_b == 0;
        nan_a = exponent_a == EXP_MAX && mantissa_a != 0;
        nan_b = exponent_b == EXP_MAX && mantissa_b != 0;
        result_wide = 0;
        special_result_comb = 0;

        if (nan_a || nan_b || (infinity_a && infinity_b && sign_a != sign_b)) {
            result_wide = (EXP_MAX << MANT_WIDTH) | 1;
        }
        else if (infinity_a) {
            result_wide = (sign_a ? SIGN_MASK : 0) | (EXP_MAX << MANT_WIDTH);
        }
        else if (infinity_b) {
            result_wide = (sign_b ? SIGN_MASK : 0) | (EXP_MAX << MANT_WIDTH);
        }
        else if (zero_a && zero_b) {
            result_wide = sign_a && sign_b ? SIGN_MASK : 0;
        }
        else if (zero_a) {
            result_wide = raw_b;
        }
        else if (zero_b) {
            result_wide = raw_a;
        }
        special_result_comb = result_wide.bits(W - 1, 0);
        return special_result_comb;
    }

    cpphdl::logic<W> result_comb;
    cpphdl::logic<W>& result_comb_func()
    {
        uint64_t sum;
        uint64_t magnitude;
        uint64_t retained;
        cpphdl::logic<64> result_raw;
        uint64_t discarded;
        int exponent;
        bool negative;
        bool guard;
        bool round;
        bool sticky;
        uint8_t i;

        sum = (uint64_t)sum_reg;
        magnitude = 0;
        retained = 0;
        result_raw = 0;
        discarded = 0;
        exponent = (int)(uint64_t)exponent_reg;
        negative = (sum & (uint64_t(1) << (SUM_WIDTH - 1))) != 0;
        guard = false;
        round = false;
        sticky = false;
        result_comb = 0;

        if (special_reg) {
            result_raw = (uint64_t)special_result_reg;
        }
        else {
            magnitude = negative ? ((uint64_t(0) - sum) & SUM_MASK) : sum;
            if (magnitude == 0) {
                result_raw = 0;
            }
            else {
                if ((magnitude & (uint64_t(1) << (MANT_WIDTH + 4))) != 0) {
                    discarded = magnitude & 1;
                    magnitude >>= 1;
                    if (discarded != 0) {
                        magnitude |= 1;
                    }
                    ++exponent;
                }
                for (i = 0; i < 32; ++i) {
                    if ((magnitude & (uint64_t(1) << (MANT_WIDTH + 3))) == 0
                        && magnitude != 0 && exponent > 0) {
                        magnitude <<= 1;
                        --exponent;
                    }
                }
                if (exponent <= 0) {
                    result_raw = negative ? SIGN_MASK : 0;
                }
                else if (exponent >= (int)EXP_MAX) {
                    result_raw = (negative ? SIGN_MASK : 0) | (EXP_MAX << MANT_WIDTH);
                }
                else {
                    retained = magnitude >> 3;
                    guard = ((magnitude >> 2) & 1) != 0;
                    round = ((magnitude >> 1) & 1) != 0;
                    sticky = (magnitude & 1) != 0;
                    if (guard && (round || sticky || (retained & 1) != 0)) {
                        ++retained;
                    }
                    if (retained >= (uint64_t(1) << (MANT_WIDTH + 1))) {
                        retained >>= 1;
                        ++exponent;
                    }
                    if (exponent >= (int)EXP_MAX) {
                        result_raw = (negative ? SIGN_MASK : 0) | (EXP_MAX << MANT_WIDTH);
                    }
                    else {
                        result_raw = (negative ? SIGN_MASK : 0)
                            | ((uint64_t)exponent << MANT_WIDTH)
                            | (retained & MANT_MASK);
                    }
                }
            }
        }

        result_comb = result_raw.bits(W - 1, 0);
        return result_comb;
    }

public:
    void _work(bool reset)
    {
        if (en_in()) {
            sum_reg._next = add_lhs_comb_func() + add_rhs_comb_func();
            exponent_reg._next = exponent_comb_func();
            special_result_reg._next = special_result_comb_func();
            special_reg._next = special_comb_func();
        }

        if (reset) {
            sum_reg.clr();
            exponent_reg.clr();
            special_result_reg.clr();
            special_reg.clr();
        }
    }

    void _strobe()
    {
        sum_reg.strobe();
        exponent_reg.strobe();
        special_result_reg.strobe();
        special_reg.strobe();
    }

    void _assign() {}
};

namespace cpphdl::hls::math {
template<size_t W=32, size_t EW=8> using FpAdd = ::FpAdd<W,EW>;
}
