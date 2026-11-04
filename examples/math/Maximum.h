#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cstdint>

namespace cpphdl_examples::math {
// A reduction value carries the original position, so an arbitrary tree and
// cache partition preserve the sequential first-maximum contract. Position
// UINT32_MAX is the empty identity. Only a NaN at position zero wins; later
// NaNs are ignored, as in `if (maximum < value)` starting at input[0].
// Integer operations deliberately express FP32 ordering without a subtractor
// or a floating-point arithmetic pipeline. Signed zero compares equal.
struct MaximumMethods {
    uint64_t maximum_merge(uint64_t a, uint64_t b) const {
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
} // namespace cpphdl_examples::math
#pragma pop_macro("MAINFILE")

#if defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cpphdl.h>

class MaximumExample : public cpphdl::Module {
  public:
    _PORT(uint64_t) a_in, b_in;
    _PORT(uint64_t) result_out;
    void _assign() { result_out = _ASSIGN_COMB(result_comb_func()); }

  private:
    cpphdl_examples::math::MaximumMethods methods;

  private:
    uint64_t result_comb = 0;
    uint64_t &result_comb_func() {
        result_comb = methods.maximum_merge(a_in(), b_in());
        return result_comb;
    }
};
#pragma pop_macro("MAINFILE")
#endif

#if !defined(SYNTHESIS) && defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "../../lib/math/Fp32Reference.h"
#include <algorithm>
#include <array>
#include <random>
#ifdef VERILATOR
#include "VMaximumExample.h"
#endif
long _system_clock = 0;
int main_from_header(int, char**) {
    uint64_t a = 0, b = 0;
#ifdef VERILATOR
    VMaximumExample dut;
#else
    MaximumExample dut;
    dut.a_in = _ASSIGN(a);
    dut.b_in = _ASSIGN(b);
    dut._assign();
#endif
    auto merge = [&](uint64_t left, uint64_t right) {
        a = left;
        b = right;
#ifdef VERILATOR
        dut.a_in = a;
        dut.b_in = b;
        dut.eval();
        uint64_t result = dut.result_out;
#else
        uint64_t result = dut.result_out();
#endif
        ++_system_clock;
        return result;
    };
    std::mt19937 random(17);
    for (unsigned trial = 0; trial < 10000; ++trial) {
        std::array<uint64_t, 16> records;
        uint32_t best = 0, best_bits = 0;
        for (unsigned i = 0; i < records.size(); ++i) {
            uint32_t bits = random();
            if (trial < 256)
                bits = fp_reference::edges[(trial + i) % 16];
            records[i] = (uint64_t(i) << 32) | bits;
            if (!i || fp_reference::value(best_bits) < fp_reference::value(bits)) {
                best = i;
                best_bits = bits;
            }
        }
        uint64_t expected = (uint64_t(best) << 32) | best_bits;
        for (unsigned i = 0; i < records.size(); ++i) {
            fp_reference::check(
                "empty left", merge(cpphdl_examples::math::maximum_empty, records[i]), records[i]);
            fp_reference::check(
                "empty right", merge(records[i], cpphdl_examples::math::maximum_empty), records[i]);
        }
        std::shuffle(records.begin(), records.end(), random);
        for (unsigned width = records.size(); width > 1; width /= 2)
            for (unsigned i = 0; i < width / 2; ++i)
                records[i] = merge(records[2 * i], records[2 * i + 1]);
        fp_reference::check("first maximum", records[0], expected);
    }
    std::puts("PASS first-maximum: shuffled trees, NaNs, signed zeros, ties and empty inputs");
    return 0;
}
#pragma pop_macro("MAINFILE")
#endif
