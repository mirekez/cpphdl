#pragma once
#include <cstdint>

namespace cpphdl_examples::math {
// Signed Q16.48 arithmetic. Products retain all 128 bits before truncation.
// These bounded combinational algorithms have no internal clock latency.
struct Q48Math {
    static constexpr int64_t ONE = int64_t(1) << 48;
    static constexpr int64_t LN2 = 195103586505167LL;

    static int64_t multiply(int64_t a, int64_t b) {
        __int128 product;
        product = __int128(a) * __int128(b);
        return int64_t(product >> 48);
    }

    static int64_t divide(int64_t a, int64_t b) {
        __int128 numerator;
        numerator = __int128(a) * __int128(ONE);
        return b == 0 ? int64_t(0) : int64_t(numerator / __int128(b));
    }

    // Non-positive inputs only; positive inputs return zero. This also makes
    // inactive mux branches well-defined when all four modules evaluate.
    static int64_t exp_negative(int64_t x) {
        int64_t residual, sum, term;
        unsigned shift;
        residual = 0;
        sum = 0;
        term = 0;
        shift = 0;
        if (x <= 0 && x > -49 * LN2) {
            shift = unsigned(-x / LN2);
            residual = x + int64_t(shift) * LN2;
            sum = ONE;
            term = ONE;
            for (int n = 1; n <= 24; ++n) {
                if (term != 0) {
                    term = multiply(term, residual) / n;
                    sum += term;
                }
            }
        }
        return sum >> shift;
    }

    static unsigned leading_bit(uint64_t value) {
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        value |= value >> 32;
        value ^= value >> 1;
        return (unsigned((value & 0xffffffff00000000ULL) != 0) << 5) |
               (unsigned((value & 0xffff0000ffff0000ULL) != 0) << 4) |
               (unsigned((value & 0xff00ff00ff00ff00ULL) != 0) << 3) |
               (unsigned((value & 0xf0f0f0f0f0f0f0f0ULL) != 0) << 2) |
               (unsigned((value & 0xccccccccccccccccULL) != 0) << 1) |
               unsigned((value & 0xaaaaaaaaaaaaaaaaULL) != 0);
    }

    static int64_t inverse_sqrt(int64_t x) {
        int k;
        int64_t normalized, y, factor;
        __int128 scaled;
        bool active;
        k = 0;
        normalized = 0;
        y = 0;
        factor = 0;
        scaled = 0;
        active = x > 0;
        if (active) {
            k = (int(leading_bit(uint64_t(x))) - 48) >> 1;
            normalized = k >= 0 ? x >> (2 * k) : x << (-2 * k);
            y = normalized < 2 * ONE ? ONE : ONE / 2;
            for (unsigned i = 0; i < 8; ++i) {
                if (active) {
                    factor = 3 * ONE - multiply(normalized, multiply(y, y));
                    y = int64_t((__int128(y) * __int128(factor)) >> 49);
                    if (y <= 0) {
                        y = 1;
                        active = false;
                    }
                }
            }
            scaled = k >= 0 ? __int128(y >> k) : __int128(y) << -k;
        }
        return scaled > INT64_MAX ? INT64_MAX : int64_t(scaled);
    }

    static int64_t silu(int64_t x) {
        int64_t e;
        e = exp_negative(x < 0 ? x : -x);
        return multiply(x, divide(x < 0 ? e : ONE, ONE + e));
    }
};
} // namespace cpphdl_examples::math
