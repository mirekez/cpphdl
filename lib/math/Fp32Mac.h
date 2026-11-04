#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "Fp32Bits.h"
#include "FpAdd.h"

// Two enabled stages: FP32 product, then FpAdd's registered significand sum.
// Extra stages belong to the latency-parameterized replacement wrapper.
class Fp32Mac : public cpphdl::Module {
  public:
    _PORT(uint32_t) a_in;
    _PORT(uint32_t) b_in;
    _PORT(uint32_t) c_in;
    _PORT(bool) en_in;
    _PORT(uint32_t) result_out;
    FpAdd<32, 8> adder;
    cpphdl::reg<cpphdl::logic<32>> product, addend;
    uint32_t multiply_comb, result_comb;
    uint32_t &multiply_comb_func() {
        uint32_t a, b, ea, eb, ma, mb, sign, shift, retained, tail;
        uint64_t product_value, mantissa;
        int32_t exponent;
        a = a_in();
        b = b_in();
        ea = (a >> 23) & 255;
        eb = (b >> 23) & 255;
        ma = a & 0x7fffff;
        mb = b & 0x7fffff;
        sign = (a ^ b) & 0x80000000u;
        product_value = 0;
        mantissa = 0;
        exponent = 0;
        shift = 20;
        retained = 0;
        tail = 0;
        multiply_comb = sign;
        if ((ea == 255 && ma) || (eb == 255 && mb) || (ea == 255 && !eb) || (eb == 255 && !ea))
            multiply_comb = 0x7fc00000u;
        else if (ea == 255 || eb == 255)
            multiply_comb = sign | 0x7f800000u;
        else if (ea && eb) {
            product_value = uint64_t(ma | 0x800000u) * uint64_t(mb | 0x800000u);
            exponent = int32_t(ea) + int32_t(eb) - 127;
            if (product_value & (uint64_t(1) << 47)) {
                shift = 21;
                ++exponent;
            }
            mantissa = (product_value >> shift) |
                       uint64_t((product_value & ((uint64_t(1) << shift) - 1)) != 0);
            retained = uint32_t(mantissa >> 3);
            tail = uint32_t(mantissa) & 7;
            if (tail > 4 || (tail == 4 && (retained & 1)))
                ++retained;
            if (retained >= 0x1000000u) {
                retained >>= 1;
                ++exponent;
            }
            if (exponent >= 255)
                multiply_comb = sign | 0x7f800000u;
            else if (exponent > 0)
                multiply_comb = sign | (uint32_t(exponent) << 23) | (retained & 0x7fffff);
        }
        return multiply_comb;
    }
    uint32_t &result_comb_func() {
        result_comb = uint32_t(adder.c_out());
        // FpAdd's generic format uses payload 1; the HLS FP32 contract uses a
        // canonical quiet NaN. Keep the generic adder's existing ABI intact.
        if ((result_comb & 0x7f800000u) == 0x7f800000u && (result_comb & 0x7fffffu))
            result_comb = 0x7fc00000u;
        return result_comb;
    }
    void _assign() {
        adder.a_in = _ASSIGN(cpphdl::logic<32>(product));
        adder.b_in = _ASSIGN(cpphdl::logic<32>(addend));
        adder.en_in = _ASSIGN(en_in());
        adder._assign();
        result_out = _ASSIGN_COMB(result_comb_func());
    }
    void _work(bool reset) {
        if (en_in()) {
            product._next = multiply_comb_func();
            addend._next = c_in();
        }
        if (reset) {
            product.clr();
            addend.clr();
        }
        adder._work(reset);
    }
    void _strobe() {
        product.strobe();
        addend.strobe();
        adder._strobe();
    }
};
#pragma pop_macro("MAINFILE")

#if !defined(SYNTHESIS) && defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "Fp32Bits.h"
#ifdef VERILATOR
#include "VFp32Mac.h"
#endif
#ifdef FP_MAC_NATIVE_MODEL
#include FP_MAC_NATIVE_MODEL
#else
long _system_clock = 0;
#endif
#include <array>
#include <cstdio>
#include <stdexcept>

int main_from_header(int, char**) {
    using namespace cpphdl_examples::math;
#ifdef VERILATOR
    VFp32Mac dut;
#elif defined(FP_MAC_NATIVE_MODEL)
    cpphdl_native::Model dut;
#else
    Fp32Mac dut;
#endif
    uint32_t a = 0, b = 0, c = 0;
    bool enable = true;
#if !defined(FP_MAC_NATIVE_MODEL) && !defined(VERILATOR)
    dut.a_in = _ASSIGN(a);
    dut.b_in = _ASSIGN(b);
    dut.c_in = _ASSIGN(c);
    dut.en_in = _ASSIGN(enable);
    dut._assign();
#endif
    std::array<uint32_t, 2> expected{};
    std::array<bool, 2> valid{};
    uint32_t random = 0x786325ad;
    auto next = [&]() {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    const uint32_t edge[] = {0,           0x80000000u, 1,           0x007fffffu,
                             0x00800000u, 0x00800001u, 0x3f800000u, 0x3f800001u,
                             0xbf800000u, 0x3fffffffu, 0x7f7fffffu, 0x7f800000u,
                             0xff800000u, 0x7fc00000u, 0xffa12345u, 0x80800000u};
    for (unsigned cycle = 0; cycle < 100000; ++cycle) {
        bool reset = cycle == 0 || cycle % 997 == 0;
        enable = cycle % 13 > 3;
        if (cycle < 4096) {
            a = edge[cycle & 15];
            b = edge[(cycle >> 4) & 15];
            c = edge[(cycle >> 8) & 15];
        } else {
            a = next();
            b = next();
            c = next();
        }
        if (reset) {
            expected = {};
            valid = {};
        } else if (enable) {
            expected[1] = expected[0];
            expected[0] = fp32_add(c, fp32_multiply(a, b));
            valid[1] = valid[0];
            valid[0] = true;
        }
#ifdef VERILATOR
        dut.a_in = a;
        dut.b_in = b;
        dut.c_in = c;
        dut.en_in = enable;
        dut.reset = reset;
        dut.clk = 0;
        dut.eval();
        dut.clk = 1;
        dut.eval();
        dut.clk = 0;
        dut.eval();
        uint32_t result = dut.result_out;
#elif defined(FP_MAC_NATIVE_MODEL)
        dut.a_in[0] = a;
        dut.b_in[0] = b;
        dut.c_in[0] = c;
        dut.en_in[0] = enable;
        dut.reset[0] = reset;
        dut.clk[0] = 0;
        dut.step();
        dut.clk[0] = 1;
        dut.step();
        dut.clk[0] = 0;
        dut.step();
        uint32_t result = dut.result_out[0];
#else
        dut._work(reset);
        dut._strobe();
        ++_system_clock;
        uint32_t result = dut.result_out();
#endif
        if (valid[1] && result != expected[1]) {
            std::fprintf(stderr, "clock=%u result=%08x expected=%08x\n", cycle, result,
                         expected[1]);
            throw std::runtime_error("FP32 MAC primitive mismatch");
        }
    }
    std::puts("PASS FP32 MAC: 100000 clocks, FP32 classes, random inputs, enable stalls and reset");
    return 0;
}
#pragma pop_macro("MAINFILE")
#endif
