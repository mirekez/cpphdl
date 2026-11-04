#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "Fp32Bits.h"

namespace cpphdl_examples::math {
// Parallel priority encoder: propagate the highest set bit downward, then
// encode its one-hot boundary. No serial count increments are needed.
struct Fp32BitMethods {
    static uint32_t leading_bit(uint32_t value) {
        uint32_t one;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        one = value ^ (value >> 1);
        return (uint32_t((one & 0xffff0000u) != 0) << 4) |
               (uint32_t((one & 0xff00ff00u) != 0) << 3) |
               (uint32_t((one & 0xf0f0f0f0u) != 0) << 2) |
               (uint32_t((one & 0xccccccccu) != 0) << 1) | uint32_t((one & 0xaaaaaaaau) != 0);
    }
};
// Static methods are shared by ordinary RTL and the HLS policy adapters.
struct Fp32AddMethods {
    static uint32_t fp32_leading_bit(uint32_t value) { return Fp32BitMethods::leading_bit(value); }
    // A significand with guard, round and sticky bits fits in 27 bits; its sum
    // fits in 28. Keep alignment, cancellation and rounding in a 32-bit datapath.
    // This is the same RNE/flush-to-zero contract as fp32_add, including NaNs,
    // signed zeros and cancellation. It does not reassociate a reduction.
    static uint32_t fp32_add_narrow(uint32_t a, uint32_t b) {
        uint32_t ea, eb, sa, sb, ma, mb, larger, smaller, sign, exponent, distance;
        uint32_t lhs, rhs, shift, aligned, sum, zeros, retained, tail;
        int16_t normalized_exponent;
        bool exchange;
        ea = (a >> 23) & 255;
        eb = (b >> 23) & 255;
        sa = a & 0x80000000u;
        sb = b & 0x80000000u;
        ma = a & 0x7fffffu;
        mb = b & 0x7fffffu;
        if ((ea == 255 && ma) || (eb == 255 && mb) || (ea == 255 && eb == 255 && sa != sb))
            return 0x7fc00000u;
        if (ea == 255)
            return a;
        if (eb == 255)
            return b;
        if (!ea && !eb)
            return sa & sb;
        if (!ea)
            return b;
        if (!eb)
            return a;
        exchange = (a & 0x7fffffffu) < (b & 0x7fffffffu);
        larger = exchange ? b : a;
        smaller = exchange ? a : b;
        sign = larger & 0x80000000u;
        exponent = (larger >> 23) & 255;
        distance = exponent - ((smaller >> 23) & 255);
        lhs = ((larger & 0x7fffffu) | 0x800000u) << 3;
        rhs = ((smaller & 0x7fffffu) | 0x800000u) << 3;
        // Mask the C++ shift count even in the unselected arm. HLS sees a
        // bounded barrel shifter and sticky-bit reduction, with no 64-bit mask.
        shift = distance & 31;
        aligned =
            distance >= 27
                ? 1u
                : (distance == 0 ? rhs
                                 : ((rhs >> shift) | uint32_t((rhs << ((32 - shift) & 31)) != 0)));
        sum = ((larger ^ smaller) & 0x80000000u) ? lhs - aligned : lhs + aligned;
        if (sum == 0)
            return 0;
        normalized_exponent = int16_t(exponent);
        if (sum & 0x08000000u) {
            sum = (sum >> 1) | (sum & 1);
            ++normalized_exponent;
        } else {
            zeros = (26 - fp32_leading_bit(sum)) & 31;
            sum <<= zeros;
            normalized_exponent -= int16_t(zeros);
        }
        retained = (sum >> 3) & 0x00ffffffu;
        tail = sum & 7;
        if (tail > 4 || (tail == 4 && (retained & 1)))
            ++retained;
        if (retained >= 0x01000000u) {
            retained >>= 1;
            ++normalized_exponent;
        }
        if (normalized_exponent >= 255)
            return sign | 0x7f800000u;
        if (normalized_exponent <= 0)
            return sign;
        return sign | (uint32_t(normalized_exponent) << 23) | (retained & 0x7fffffu);
    }
};

inline uint32_t fp32_leading_bit(uint32_t value) {
    return Fp32AddMethods::fp32_leading_bit(value);
}
inline uint32_t fp32_add_narrow(uint32_t a, uint32_t b) {
    return Fp32AddMethods::fp32_add_narrow(a, b);
}
} // namespace cpphdl_examples::math
#pragma pop_macro("MAINFILE")

#if defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cpphdl.h>

// Combinational FP32 addition using only a 32-bit significand datapath.
class Fp32AddExample : public cpphdl::Module {
  public:
    _PORT(uint32_t) a_in, b_in;
    _PORT(uint32_t) result_out;
    void _assign() { result_out = _ASSIGN_COMB(result_comb_func()); }

  private:
    uint32_t result_comb = 0;
    uint32_t &result_comb_func() {
        result_comb = cpphdl_examples::math::Fp32AddMethods::fp32_add_narrow(a_in(), b_in());
        return result_comb;
    }
};
#pragma pop_macro("MAINFILE")
#endif

#if !defined(SYNTHESIS) && defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "Fp32Reference.h"
#ifdef VERILATOR
#include "VFp32AddExample.h"
#endif
long _system_clock = 0;
int main_from_header(int, char**) {
    uint32_t a = 0, b = 0;
#ifdef VERILATOR
    VFp32AddExample dut;
#else
    Fp32AddExample dut;
    dut.a_in = _ASSIGN(a);
    dut.b_in = _ASSIGN(b);
    dut._assign();
#endif
    auto check = [&]() {
#ifdef VERILATOR
        dut.a_in = a;
        dut.b_in = b;
        dut.eval();
        uint32_t result = dut.result_out;
#else
        uint32_t result = dut.result_out();
#endif
        fp_reference::check("narrow add", result, fp_reference::add(a, b));
        ++_system_clock;
    };
    for (auto x : fp_reference::edges)
        for (auto y : fp_reference::edges) {
            a = x;
            b = y;
            check();
        }
    for (uint32_t ea = 1; ea < 255; ++ea)
        for (uint32_t eb = 1; eb < 255; ++eb) {
            a = (ea << 23) | 0x7fffffu;
            b = eb << 23;
            check();
            b ^= 0x80000000u;
            check();
            b = a ^ 0x80000000u;
            check();
            b = (a - 1) ^ 0x80000000u;
            check();
        }
    uint32_t state = 0x17982ab5;
    for (unsigned i = 0; i < 100000; ++i) {
        a = fp_reference::random(state);
        b = fp_reference::random(state);
        check();
    }
    std::puts("PASS narrow FP32 add: special values, exponent gaps, cancellation and random pairs");
    return 0;
}
#pragma pop_macro("MAINFILE")
#endif
