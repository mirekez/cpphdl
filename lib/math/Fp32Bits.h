#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cstdint>

namespace cpphdl_examples::math {
// Static methods are shared by ordinary RTL and the HLS policy adapters.
struct Fp32BitsMethods {
    static uint32_t leading_zeros(uint32_t x) {
        uint32_t count;
        count = 0;
        if (!x)
            return 32;
        if (!(x >> 16)) {
            count += 16;
            x <<= 16;
        }
        if (!(x >> 24)) {
            count += 8;
            x <<= 8;
        }
        if (!(x >> 28)) {
            count += 4;
            x <<= 4;
        }
        if (!(x >> 30)) {
            count += 2;
            x <<= 2;
        }
        if (!(x >> 31))
            count += 1;
        return count;
    }
    // RNE and canonical NaNs; exponent-zero FP32 values flush to signed zero,
    // following lib/math/FpAdd.h. FP16 conversion is exact, including
    // subnormals. Pure integer operations allow HLS to synthesize actual FP32 logic.
    static uint32_t fp16_to_fp32(uint32_t raw) {
        uint32_t sign, exp, mant, shift;
        sign = (raw & 0x8000u) << 16;
        exp = (raw >> 10) & 31;
        mant = raw & 1023;
        if (exp == 31)
            return sign | 0x7f800000u | (mant << 13);
        if (exp != 0)
            return sign | ((exp + 112) << 23) | (mant << 13);
        if (!mant)
            return sign;
        shift = leading_zeros(mant) - 21;
        return sign | ((113 - shift) << 23) | ((mant << (shift + 13)) & 0x7fffffu);
    }
    static uint64_t shift_jam(uint64_t x, uint32_t shift) {
        if (shift == 0)
            return x;
        if (shift >= 64)
            return x != 0;
        return (x >> shift) | ((x & ((uint64_t(1) << shift) - 1)) != 0);
    }
    static uint32_t fp32_round(uint32_t sign, int32_t exp, uint64_t mant) {
        uint64_t retained, tail;
        retained = mant >> 3;
        tail = mant & 7;
        if (tail > 4 || (tail == 4 && (retained & 1)))
            ++retained;
        if (retained >= (uint64_t(1) << 24)) {
            retained >>= 1;
            ++exp;
        }
        if (exp >= 255)
            return sign | 0x7f800000u;
        if (exp <= 0)
            return sign;
        return sign | (uint32_t(exp) << 23) | (uint32_t(retained) & 0x7fffffu);
    }
    static uint32_t fp32_add(uint32_t a, uint32_t b) {
        uint32_t ea, eb, sa, sb, ma, mb, swap, shift, sign;
        uint64_t aa, bb, sum;
        int32_t exponent;
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
        if ((a & 0x7fffffffu) < (b & 0x7fffffffu)) {
            swap = a;
            a = b;
            b = swap;
        }
        ea = (a >> 23) & 255;
        eb = (b >> 23) & 255;
        sa = a & 0x80000000u;
        sb = b & 0x80000000u;
        aa = uint64_t((a & 0x7fffffu) | 0x800000u) << 3;
        bb = shift_jam(uint64_t((b & 0x7fffffu) | 0x800000u) << 3, ea - eb);
        exponent = int32_t(ea);
        sign = sa;
        sum = sa == sb ? aa + bb : aa - bb;
        if (sum == 0)
            return 0;
        if (sum & (uint64_t(1) << 27)) {
            sum = shift_jam(sum, 1);
            ++exponent;
        } else {
            shift = leading_zeros(uint32_t(sum)) - 5;
            sum <<= shift;
            exponent -= int32_t(shift);
        }
        return fp32_round(sign, exponent, sum);
    }
    static uint32_t fp32_multiply(uint32_t a, uint32_t b) {
        uint32_t ea, eb, ma, mb, sign, shift;
        uint64_t product;
        int32_t exponent;
        ea = (a >> 23) & 255;
        eb = (b >> 23) & 255;
        ma = a & 0x7fffffu;
        mb = b & 0x7fffffu;
        sign = (a ^ b) & 0x80000000u;
        if ((ea == 255 && ma) || (eb == 255 && mb) || (ea == 255 && !eb) || (eb == 255 && !ea))
            return 0x7fc00000u;
        if (ea == 255 || eb == 255)
            return sign | 0x7f800000u;
        if (!ea || !eb)
            return sign;
        product = uint64_t(ma | 0x800000u) * uint64_t(mb | 0x800000u);
        exponent = int32_t(ea) + int32_t(eb) - 127;
        shift = 20;
        if (product & (uint64_t(1) << 47)) {
            shift = 21;
            ++exponent;
        }
        return fp32_round(sign, exponent, shift_jam(product, shift));
    }
};

inline uint32_t leading_zeros(uint32_t x) {
    return Fp32BitsMethods::leading_zeros(x);
}
inline uint32_t fp16_to_fp32(uint32_t raw) {
    return Fp32BitsMethods::fp16_to_fp32(raw);
}
inline uint64_t shift_jam(uint64_t x, uint32_t shift) {
    return Fp32BitsMethods::shift_jam(x, shift);
}
inline uint32_t fp32_round(uint32_t sign, int32_t exp, uint64_t mant) {
    return Fp32BitsMethods::fp32_round(sign, exp, mant);
}
inline uint32_t fp32_add(uint32_t a, uint32_t b) {
    return Fp32BitsMethods::fp32_add(a, b);
}
inline uint32_t fp32_multiply(uint32_t a, uint32_t b) {
    return Fp32BitsMethods::fp32_multiply(a, b);
}
} // namespace cpphdl_examples::math
#pragma pop_macro("MAINFILE")

#if defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cpphdl.h>

class Fp32BitsExample : public cpphdl::Module {
  public:
    _PORT(uint32_t) a_in, b_in;
    _PORT(uint32_t) sum_out, product_out, half_out;
    void _assign() {
        sum_out = _ASSIGN_COMB(sum_comb_func());
        product_out = _ASSIGN_COMB(product_comb_func());
        half_out = _ASSIGN_COMB(half_comb_func());
    }

  private:
    uint32_t sum_comb = 0;
    uint32_t &sum_comb_func() {
        sum_comb = cpphdl_examples::math::Fp32BitsMethods::fp32_add(a_in(), b_in());
        return sum_comb;
    }
    uint32_t product_comb = 0;
    uint32_t &product_comb_func() {
        product_comb = cpphdl_examples::math::Fp32BitsMethods::fp32_multiply(a_in(), b_in());
        return product_comb;
    }
    uint32_t half_comb = 0;
    uint32_t &half_comb_func() {
        half_comb = cpphdl_examples::math::Fp32BitsMethods::fp16_to_fp32(a_in());
        return half_comb;
    }
};
#pragma pop_macro("MAINFILE")
#endif

#if !defined(SYNTHESIS) && defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "Fp32Reference.h"
#ifdef VERILATOR
#include "VFp32BitsExample.h"
#endif
long _system_clock = 0;
int main_from_header(int, char**) {
    uint32_t a = 0, b = 0;
#ifdef VERILATOR
    VFp32BitsExample dut;
#else
    Fp32BitsExample dut;
    dut.a_in = _ASSIGN(a);
    dut.b_in = _ASSIGN(b);
    dut._assign();
#endif
    auto check = [&]() {
#ifdef VERILATOR
        dut.a_in = a;
        dut.b_in = b;
        dut.eval();
        auto sum = dut.sum_out, product = dut.product_out, half = dut.half_out;
#else
        auto sum = dut.sum_out(), product = dut.product_out(), half = dut.half_out();
#endif
        fp_reference::check("add", sum, fp_reference::add(a, b));
        fp_reference::check("multiply", product, fp_reference::multiply(a, b));
        fp_reference::check("half", half, fp_reference::half(uint16_t(a)));
        ++_system_clock;
    };
    for (auto x : fp_reference::edges)
        for (auto y : fp_reference::edges) {
            a = x;
            b = y;
            check();
        }
    for (a = 0; a < 65536; ++a)
        check();
    uint32_t state = 713;
    for (unsigned i = 0; i < 100000; ++i) {
        a = fp_reference::random(state);
        b = fp_reference::random(state);
        check();
    }
    std::puts("PASS FP32 add/multiply, all FP16 encodings and 100000 random pairs");
    return 0;
}
#pragma pop_macro("MAINFILE")
#endif
