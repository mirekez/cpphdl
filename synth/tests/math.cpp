#include "cpphdl.h"
#include <cstdint>

class SynthMath : public cpphdl::Module {
public:
    _PORT(uint8_t) a_in;
    _PORT(uint8_t) b_in;
    _PORT(uint64_t) wide_in;
    _PORT(bool) en_in;
    _PORT(uint16_t) sum_out = _ASSIGN(uint16_t(a_in()) + uint16_t(b_in()));
    _PORT(uint16_t) difference_out = _ASSIGN(uint16_t(a_in() - b_in()));
    _PORT(uint16_t) product_out = _ASSIGN(uint16_t(a_in()) * uint16_t(b_in()));
    _PORT(uint8_t) quotient_out = _ASSIGN(b_in() ? a_in() / b_in() : 0);
    _PORT(uint8_t) remainder_out = _ASSIGN(b_in() ? a_in() % b_in() : 0);
    _PORT(int16_t) signed_quotient_out = _ASSIGN(b_in() ? int8_t(a_in()) / int8_t(b_in()) : 0);
    _PORT(int16_t) signed_remainder_out = _ASSIGN(b_in() ? int8_t(a_in()) % int8_t(b_in()) : 0);
    _PORT(bool) less_out = _ASSIGN(int8_t(a_in()) < int8_t(b_in()));
    _PORT(int16_t) signed_product_out = _ASSIGN(int16_t(int8_t(a_in())) * int16_t(int8_t(b_in())));
    _PORT(int8_t) arithmetic_shift_out = _ASSIGN(int8_t(a_in()) >> (b_in() & 7));
    _PORT(uint8_t) saturated_out = _ASSIGN(saturating_add(a_in(), b_in()));
    _PORT(uint8_t) root_out = _ASSIGN(integer_sqrt((uint16_t(a_in()) << 8) | b_in()));
    _PORT(uint8_t) population_out = _ASSIGN(population(a_in()));
    _PORT(uint64_t) wide_sum_out = _ASSIGN(wide_in() + uint64_t(a_in()));
    _PORT(uint64_t) wide_shift_out = _ASSIGN(wide_in() >> (b_in() & 63));
    _PORT(uint32_t) accumulated_out = _ASSIGN_REG(accumulated);

    cpphdl::reg<cpphdl::u<32>> accumulated;

    static uint8_t saturating_add(uint8_t a, uint8_t b) {
        uint16_t sum = uint16_t(a) + b;
        return sum > 255 ? 255 : uint8_t(sum);
    }
    static uint8_t population(uint8_t value) {
        uint8_t result = 0;
        for (unsigned i = 0; i < 8; ++i) result += (value >> i) & 1;
        return result;
    }
    static uint8_t integer_sqrt(uint16_t value) {
        uint32_t root = 0;
        uint32_t remainder = 0;
        uint32_t trial;
        for (unsigned i = 0; i < 8; ++i) {
            remainder = (remainder << 2) | ((value >> (14 - 2 * i)) & 3);
            trial = (root << 2) | 1;
            root <<= 1;
            if (remainder >= trial) {
                remainder -= trial;
                root |= 1;
            }
        }
        return uint8_t(root);
    }
    void _work(bool reset) {
        accumulated._next = accumulated;
        if (en_in()) accumulated._next = uint32_t(accumulated) + uint32_t(a_in()) * b_in();
        if (reset) accumulated._next = 0;
    }
    void _strobe() { accumulated.strobe(); }
};

SynthMath cpphdl_top;

#ifdef SYNTH_MATH_RUN
#include <cstdio>
#include <cstdlib>
#ifdef SYNTH_MATH_VERILATOR
#include "VSynthMath.h"
#endif
#ifdef SYNTH_MATH_GRAPH
#include "model.h"
#endif
long _system_clock = 0;

static void check(const char* name, uint64_t actual, uint64_t expected, unsigned sample) {
    if (actual != expected) {
        std::fprintf(stderr, "%s sample=%u actual=%llu expected=%llu\n", name, sample,
                     (unsigned long long)actual, (unsigned long long)expected);
        std::exit(1);
    }
}

int main() {
    uint8_t a = 0, b = 0;
    uint64_t wide = 0;
    bool enable = false;
    uint32_t expected_accumulated = 0;
    uint64_t random = 0x981277bac37513ull;
#ifdef SYNTH_MATH_VERILATOR
    VSynthMath dut;
#endif
#ifdef SYNTH_MATH_GRAPH
    cpphdl_native::Model graph;
#endif
    cpphdl_top.a_in = _ASSIGN(a);
    cpphdl_top.b_in = _ASSIGN(b);
    cpphdl_top.wide_in = _ASSIGN(wide);
    cpphdl_top.en_in = _ASSIGN(enable);
    cpphdl_top._assign();
    for (unsigned sample = 0; sample < 65536; ++sample) {
        a = sample >> 8;
        b = sample;
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        wide = sample % 97 ? random : UINT64_MAX;
        enable = (sample % 3) != 0;
        bool reset = sample % 257 == 0;
        if (enable) expected_accumulated += uint32_t(a) * b;
        if (reset) expected_accumulated = 0;
#ifdef SYNTH_MATH_VERILATOR
        dut.clk = 0; dut.work_reset = reset;
        dut.a = a; dut.b = b; dut.wide = wide; dut.en = enable;
        dut.eval();
        if (sample) check("hold between edges", dut.accumulated, uint32_t(cpphdl_top.accumulated), sample);
        dut.clk = 1; dut.eval();
        dut.clk = 0; dut.eval();
#endif
#ifdef SYNTH_MATH_GRAPH
        graph.a[0] = a; graph.b[0] = b; graph.en[0] = enable;
        graph.wide[0] = uint32_t(wide); graph.wide[1] = wide >> 32;
        graph.work_reset[0] = reset; graph.step();
#endif
        cpphdl_top._work(reset);
        cpphdl_top._strobe();
        uint32_t root = 0;
        while ((root + 1) * (root + 1) <= sample) ++root;
        unsigned population = 0;
        for (unsigned v = a; v; v >>= 1) population += v & 1;
#define CHECK(field, expected) \
        check(#field " native", uint64_t(cpphdl_top.field##_out()), uint64_t(expected), sample); \
        CHECK_BACKEND(field)
#ifdef SYNTH_MATH_VERILATOR
#define CHECK_BACKEND(field) check(#field " gates", uint64_t(dut.field), \
            uint64_t(cpphdl_top.field##_out()) & ((sizeof(dut.field) == 8) ? UINT64_MAX : \
            (UINT64_MAX >> (64 - 8 * sizeof(dut.field)))), sample);
#elif defined(SYNTH_MATH_GRAPH)
#define CHECK_BACKEND(field) check(#field " graph", graph.field[0], uint32_t(cpphdl_top.field##_out()), sample);
#else
#define CHECK_BACKEND(field)
#endif
        CHECK(sum, unsigned(a) + b);
        CHECK(difference, uint16_t(int(a) - b));
        CHECK(product, unsigned(a) * b);
        CHECK(quotient, b ? a / b : 0);
        CHECK(remainder, b ? a % b : 0);
        CHECK(less, int8_t(a) < int8_t(b));
        // Signed outputs use explicitly sized comparisons below.
        check("signed product", cpphdl_top.signed_product_out(), int16_t(int8_t(a)) * int16_t(int8_t(b)), sample);
        check("signed quotient", cpphdl_top.signed_quotient_out(), b ? int8_t(a) / int8_t(b) : 0, sample);
        check("signed remainder", cpphdl_top.signed_remainder_out(), b ? int8_t(a) % int8_t(b) : 0, sample);
        check("arithmetic shift", cpphdl_top.arithmetic_shift_out(), int8_t(a) >> (b & 7), sample);
#ifdef SYNTH_MATH_VERILATOR
        check("signed product gates", dut.signed_product, uint16_t(cpphdl_top.signed_product_out()), sample);
        check("signed quotient gates", dut.signed_quotient, uint16_t(cpphdl_top.signed_quotient_out()), sample);
        check("signed remainder gates", dut.signed_remainder, uint16_t(cpphdl_top.signed_remainder_out()), sample);
        check("arithmetic shift gates", dut.arithmetic_shift, uint8_t(cpphdl_top.arithmetic_shift_out()), sample);
#endif
#ifdef SYNTH_MATH_GRAPH
        check("signed product graph", graph.signed_product[0], uint16_t(cpphdl_top.signed_product_out()), sample);
        check("signed quotient graph", graph.signed_quotient[0], uint16_t(cpphdl_top.signed_quotient_out()), sample);
        check("signed remainder graph", graph.signed_remainder[0], uint16_t(cpphdl_top.signed_remainder_out()), sample);
        check("arithmetic shift graph", graph.arithmetic_shift[0], uint8_t(cpphdl_top.arithmetic_shift_out()), sample);
#endif
        CHECK(saturated, unsigned(a) + b > 255 ? 255 : unsigned(a) + b);
        CHECK(root, root);
        CHECK(population, population);
        CHECK(wide_sum, wide + a);
        CHECK(wide_shift, wide >> (b & 63));
        CHECK(accumulated, expected_accumulated);
#ifdef SYNTH_MATH_GRAPH
        check("wide sum upper", graph.wide_sum[1], uint32_t((wide + a) >> 32), sample);
        check("wide shift upper", graph.wide_shift[1], uint32_t((wide >> (b & 63)) >> 32), sample);
#endif
        ++_system_clock;
    }
    std::puts("math: 65536 operand pairs; arithmetic, sqrt, saturation, shifts, popcount, reset and enable passed");
}
#endif
