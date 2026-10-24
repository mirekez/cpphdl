#pragma once
#include <cstdint>

namespace cpphdl::hls::math
{
// A reduction value carries the original position, so an arbitrary tree and
// cache partition preserve the sequential first-maximum contract. Position
// UINT32_MAX is the empty identity. Only a NaN at position zero wins; later
// NaNs are ignored, as in `if (maximum < value)` starting at input[0].
// Integer operations deliberately express FP32 ordering without a subtractor
// or a floating-point arithmetic pipeline. Signed zero compares equal.
struct MaximumMethods
{
    uint64_t maximum_merge(uint64_t a, uint64_t b) const
    {
        uint32_t ai, bi, av, bv, ak, bk;
        bool an, bn, equal, take_b;
        ai = uint32_t(a >> 32);
        bi = uint32_t(b >> 32);
        av = uint32_t(a);
        bv = uint32_t(b);
        an = (av & 0x7fffffffu) > 0x7f800000u;
        bn = (bv & 0x7fffffffu) > 0x7f800000u;
        ak = (av & 0x80000000u) ? ~av : (av ^ 0x80000000u);
        bk = (bv & 0x80000000u) ? ~bv : (bv ^ 0x80000000u);
        equal = av == bv || ((av & 0x7fffffffu) == 0 && (bv & 0x7fffffffu) == 0);
        // One data mux after parallel compare/valid predicates. A cascade of
        // early-return muxes is needlessly deep on FPGA (and limits inlining).
        take_b = bi != 0xffffffffu &&
                 (ai == 0xffffffffu ||
                  (!(ai == 0 && an) &&
                   ((bi == 0 && bn) || (!bn && (an || (equal ? bi < ai : ak < bk))))));
        return take_b ? b : a;
    }
};
inline constexpr uint64_t maximum_empty = 0xffffffff00000000ull;
} // namespace cpphdl::hls::math
