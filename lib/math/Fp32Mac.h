#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "Fp32Bits.h"
#include "FpAdd.h"

// Two enabled stages by default: product, then FpAdd's significand sum.
// PIPELINE_ADD inserts FpAdd's internal normalization/rounding boundary.
// PIPELINE_PRODUCT captures the raw DSP product before FP32 rounding.
// PIPELINE_ALIGN separates the adder alignment and significand sum.
// The replacement wrapper accounts for all selected internal stages.
template<bool PIPELINE_ADD=false, bool PIPELINE_PRODUCT=false, bool PIPELINE_ALIGN=false>
class Fp32Mac : public cpphdl::Module {
  public:
    _PORT(uint32_t) a_in;
    _PORT(uint32_t) b_in;
    _PORT(uint32_t) c_in;
    _PORT(bool) en_in;
    _PORT(uint32_t) result_out;
    FpAdd<32, 8, true, PIPELINE_ADD, PIPELINE_ALIGN> adder;
    cpphdl::reg<cpphdl::logic<32>> product, addend;
    cpphdl::reg<cpphdl::logic<128>> raw_product;
    cpphdl::reg<cpphdl::logic<32>> raw_addend;
    cpphdl::logic<128> raw_product_comb;
    uint32_t multiply_comb, result_comb;
    cpphdl::logic<128>& raw_product_comb_func() {
        uint32_t a, b, ea, eb, ma, mb, sign, special_value;
        uint64_t product_value;
        int32_t exponent;
        bool special;
        a = a_in(); b = b_in();
        ea = (a >> 23) & 255; eb = (b >> 23) & 255;
        ma = a & 0x7fffff; mb = b & 0x7fffff;
        sign = (a ^ b) & 0x80000000u;
        // No normalization, rounding or class-selection mux lies between
        // this multiply and its optional register. Metadata travels beside it.
        product_value = uint64_t(ma | 0x800000u) * uint64_t(mb | 0x800000u);
        exponent = int32_t(ea) + int32_t(eb) - 127;
        special = true; special_value = sign;
        if ((ea == 255 && ma) || (eb == 255 && mb) || (ea == 255 && !eb) || (eb == 255 && !ea))
            special_value = 0x7fc00000u;
        else if (ea == 255 || eb == 255)
            special_value = sign | 0x7f800000u;
        else if (ea && eb) special = false;
        raw_product_comb = cpphdl::logic<128>(product_value);
        raw_product_comb |= cpphdl::logic<128>(uint32_t(exponent)) << 48;
        raw_product_comb |= cpphdl::logic<128>(special_value) << 80;
        raw_product_comb |= cpphdl::logic<128>(special) << 112;
        raw_product_comb |= cpphdl::logic<128>(sign != 0) << 113;
        return raw_product_comb;
    }
    uint32_t &multiply_comb_func() {
        cpphdl::logic<128> raw;
        uint32_t sign, shift, retained, tail;
        uint64_t product_value, mantissa;
        int32_t exponent;
        if (PIPELINE_PRODUCT) raw = raw_product;
        else raw = raw_product_comb_func();
        product_value = (uint64_t)raw.bits(47,0);
        exponent = int32_t(uint32_t((uint64_t)raw.bits(79,48)));
        sign = (uint64_t)raw.bits(113,113) ? 0x80000000u : 0;
        mantissa = 0; shift = 20; retained = 0; tail = 0;
        multiply_comb = sign;
        if ((uint64_t)raw.bits(112,112)) multiply_comb = uint32_t((uint64_t)raw.bits(111,80));
        else {
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
            if (PIPELINE_PRODUCT) {
                raw_product._next = raw_product_comb_func();
                raw_addend._next = c_in();
                addend._next = raw_addend;
            } else addend._next = c_in();
        }
        if (reset) {
            product.clr();
            addend.clr();
            raw_product.clr();
            raw_addend.clr();
        }
        adder._work(reset);
    }
    void _strobe() {
        product.strobe();
        addend.strobe();
        raw_product.strobe();
        raw_addend.strobe();
        adder._strobe();
    }
};
#pragma pop_macro("MAINFILE")

#if defined(SYNTHESIS) && defined(MAINFILE)
// The inline testbench is excluded during conversion. Instantiate its default
// configuration explicitly so the converter emits the standalone DUT.
template class Fp32Mac<>;
#endif

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
#ifndef FP_MAC_PIPELINE_ADD
#define FP_MAC_PIPELINE_ADD 0
#endif
#ifndef FP_MAC_PIPELINE_ALIGN
#define FP_MAC_PIPELINE_ALIGN 0
#endif
#ifndef FP_MAC_PIPELINE_PRODUCT
#define FP_MAC_PIPELINE_PRODUCT 0
#endif

int main_from_header(int, char**) {
    using namespace cpphdl_examples::math;
#ifdef VERILATOR
    VFp32Mac dut;
#elif defined(FP_MAC_NATIVE_MODEL)
    cpphdl_native::Model dut;
#else
    Fp32Mac<FP_MAC_PIPELINE_ADD,FP_MAC_PIPELINE_PRODUCT,FP_MAC_PIPELINE_ALIGN> dut;
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
    constexpr unsigned stages=2+FP_MAC_PIPELINE_ADD+FP_MAC_PIPELINE_PRODUCT+FP_MAC_PIPELINE_ALIGN;
    std::array<uint32_t, stages> expected{};
    std::array<bool, stages> valid{};
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
            for(unsigned i=stages-1;i;--i) {expected[i]=expected[i-1];valid[i]=valid[i-1];}
            expected[0] = fp32_add(c, fp32_multiply(a, b));
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
        if (valid[stages-1] && result != expected[stages-1]) {
            std::fprintf(stderr, "clock=%u result=%08x expected=%08x\n", cycle, result,
                         expected[stages-1]);
            throw std::runtime_error("FP32 MAC primitive mismatch");
        }
    }
    std::puts("PASS FP32 MAC: 100000 clocks, FP32 classes, random inputs, enable stalls and reset");
    return 0;
}
#pragma pop_macro("MAINFILE")
#endif
