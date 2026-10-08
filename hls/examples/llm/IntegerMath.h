// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

namespace integer_llm {
using Q = int64_t;
using Wide = __int128;
using UWide = __uint128_t;
constexpr Q one = Q(1) << 48;

inline Q add(Q a, Q b) { return Q(uint64_t(a) + uint64_t(b)); }
inline Q multiply(Q a, Q b) { return Q((Wide(a) * b) >> 48); }

// Unsigned accumulation defines wraparound without signed C++ overflow.
inline UWide accum(UWide sum, Q a, Q b) { return sum + UWide(Wide(a) * b); }

// Row-major W[rows,depth] times X[depth,columns]. Round once per output.
inline void mmul(Q* output, const Q* weights, const Q* input,
                 unsigned rows, unsigned columns, unsigned depth) {
    for (unsigned r = 0; r < rows; ++r) {
        for (unsigned c = 0; c < columns; ++c) {
            UWide sum = 0;
            for (unsigned k = 0; k < depth; ++k)
                sum = accum(sum, weights[r * depth + k], input[k * columns + c]);
            output[r * columns + c] = Q(Wide(sum) >> 48);
        }
    }
}

struct WideAdd {
    UWide command(UWide lhs, UWide rhs, UWide) const { return lhs + rhs; }
};

struct ScalarMath {
    uint64_t command(uint64_t lhs, uint64_t rhs, uint64_t operation) const;
};

#ifdef __clang__
[[clang::annotate("CPPHDL_BLACKBOX=llm_q48_divide:0")]]
#endif
inline Q divide(Q a, Q b) { return Q((Wide(a) * one) / b); }

// Same range reduction and iteration counts as intllm/src/qmath.hpp and
// nmicic/int-llm fp_inv_sqrt. See NOTICE.md for provenance.
#ifdef __clang__
[[clang::annotate("CPPHDL_BLACKBOX=llm_q48_exp_negative:0")]]
#endif
inline Q exp_negative(Q x) {
    constexpr Q ln2 = 195103586505167LL;
    if (x <= -49 * ln2) return 0;
    int shift = int(-x / ln2);
    Q residual = x + shift * ln2;
    Q sum = one, term = one;
    for (int n = 1; n <= 24; ++n) {
        term = multiply(term, residual) / n;
        sum += term;
        if (term == 0) break;
    }
    return sum >> shift;
}

#ifdef __clang__
[[clang::annotate("CPPHDL_BLACKBOX=llm_q48_silu:0")]]
#endif
inline Q silu(Q x) {
    Q e = exp_negative(x < 0 ? x : -x);
    return multiply(x, divide(x < 0 ? e : one, one + e));
}

#ifdef __clang__
[[clang::annotate("CPPHDL_BLACKBOX=llm_q48_inverse_sqrt:0")]]
#endif
inline Q inverse_sqrt(Q x) {
    if (x <= 0) return 0;
    int k = (63 - __builtin_clzll(uint64_t(x)) - 48) >> 1;
    Q normalized = k >= 0 ? x >> (2 * k) : x << (-2 * k);
    Q y = normalized < 2 * one ? one : one / 2;
    for (int i = 0; i < 8; ++i) {
        Q factor = 3 * one - multiply(normalized, multiply(y, y));
        y = Q((Wide(y) * factor) >> 49);
        if (y <= 0) { y = 1; break; }
    }
    Wide result = k >= 0 ? Wide(y >> k) : Wide(y) << -k;
    return result > INT64_MAX ? INT64_MAX : Q(result);
}

inline uint64_t ScalarMath::command(uint64_t lhs, uint64_t rhs, uint64_t operation) const {
    if (operation == 0) return uint64_t(add(Q(lhs), Q(rhs)));
    if (operation == 1) return uint64_t(multiply(Q(lhs), Q(rhs)));
    if (operation == 2) return uint64_t(divide(Q(lhs), Q(rhs)));
    if (operation == 3) return uint64_t(exp_negative(Q(lhs)));
    if (operation == 4) return uint64_t(inverse_sqrt(Q(lhs)));
    return uint64_t(silu(Q(lhs)));
}

// Keep the full product: rounding each multiply would change the original
// intllm matrix-vector sum, which rounds only once at the end of the row.
struct Product {
    UWide command(uint64_t lhs, uint64_t rhs, uint64_t) const {
        return UWide(Wide(Q(lhs)) * Q(rhs));
    }
};
}
