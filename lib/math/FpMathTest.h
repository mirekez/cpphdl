#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

template<size_t W, size_t EW>
struct FpMathTestFormat
{
    static constexpr size_t MANT_WIDTH = W - EW - 1;
    static constexpr uint64_t MANT_MASK = (uint64_t(1) << MANT_WIDTH) - 1;
    static constexpr uint64_t EXP_MASK = (uint64_t(1) << EW) - 1;
    static constexpr uint64_t SIGN_MASK = uint64_t(1) << (W - 1);
    static constexpr uint64_t RAW_MASK = (uint64_t(1) << W) - 1;
    static constexpr int EXP_BIAS = (1 << (EW - 1)) - 1;

    static_assert(W >= 4 && W <= 32, "test format width must be 4..32");
    static_assert(EW >= 2 && EW < W - 1, "invalid test exponent width");

    static bool sign(uint64_t raw)
    {
        return (raw & SIGN_MASK) != 0;
    }

    static uint64_t exponent(uint64_t raw)
    {
        return (raw >> MANT_WIDTH) & EXP_MASK;
    }

    static uint64_t mantissa(uint64_t raw)
    {
        return raw & MANT_MASK;
    }

    static uint64_t zero(bool negative = false)
    {
        return negative ? SIGN_MASK : 0;
    }

    static uint64_t one(bool negative = false)
    {
        return (negative ? SIGN_MASK : 0)
            | (uint64_t(EXP_BIAS) << MANT_WIDTH);
    }

    static uint64_t infinity(bool negative = false)
    {
        return (negative ? SIGN_MASK : 0) | (EXP_MASK << MANT_WIDTH);
    }

    static uint64_t nan()
    {
        return (EXP_MASK << MANT_WIDTH) | 1;
    }

    static bool is_nan(uint64_t raw)
    {
        return exponent(raw) == EXP_MASK && mantissa(raw) != 0;
    }

    static double to_double(uint64_t raw)
    {
        uint64_t exp;
        uint64_t mant;
        double magnitude;

        exp = exponent(raw);
        mant = mantissa(raw);
        if (exp == EXP_MASK) {
            if (mant != 0) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            return sign(raw) ? -std::numeric_limits<double>::infinity()
                             : std::numeric_limits<double>::infinity();
        }
        if (exp == 0) {
            magnitude = std::ldexp((double)mant, 1 - EXP_BIAS - (int)MANT_WIDTH);
        }
        else {
            magnitude = std::ldexp(1.0 + (double)mant / (double)(uint64_t(1) << MANT_WIDTH),
                (int)exp - EXP_BIAS);
        }
        return sign(raw) ? -magnitude : magnitude;
    }

    static uint64_t from_double(double value)
    {
        bool negative;
        double magnitude;
        double normalized;
        double fraction;
        double floor_fraction;
        uint64_t mant;
        uint64_t exp;
        int binary_exp;
        int encoded_exp;

        if (std::isnan(value)) {
            return nan();
        }
        negative = std::signbit(value);
        magnitude = std::fabs(value);
        if (std::isinf(magnitude)) {
            return infinity(negative);
        }
        if (magnitude == 0.0) {
            return zero(negative);
        }

        normalized = std::frexp(magnitude, &binary_exp) * 2.0;
        --binary_exp;
        encoded_exp = binary_exp + EXP_BIAS;
        if (encoded_exp <= 0) {
            return zero(negative);
        }
        if (encoded_exp >= (int)EXP_MASK) {
            return infinity(negative);
        }

        fraction = (normalized - 1.0) * (double)(uint64_t(1) << MANT_WIDTH);
        floor_fraction = std::floor(fraction);
        mant = (uint64_t)floor_fraction;
        if (fraction - floor_fraction > 0.5
            || (fraction - floor_fraction == 0.5 && (mant & 1))) {
            ++mant;
        }
        exp = (uint64_t)encoded_exp;
        if (mant > MANT_MASK) {
            mant = 0;
            ++exp;
            if (exp >= EXP_MASK) {
                return infinity(negative);
            }
        }
        return (negative ? SIGN_MASK : 0) | (exp << MANT_WIDTH) | mant;
    }
};

inline uint64_t fp_math_random_step(uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}
