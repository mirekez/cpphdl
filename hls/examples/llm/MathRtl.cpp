#include "MathRtl.h"

#ifndef SYNTHESIS
#include "IntegerMath.h"
#include <cstdio>
#include <random>
#include <stdexcept>
#ifdef VERILATOR
#include "VMathRtl.h"
#endif

long _system_clock = 0;

int main() {
#ifdef VERILATOR
    VMathRtl dut;
#else
    MathRtl dut;
#endif
    uint64_t lhs = 0, rhs = 0;
    unsigned checked = 0;
#ifndef VERILATOR
    dut.lhs_in = _ASSIGN(lhs);
    dut.rhs_in = _ASSIGN(rhs);
    dut._assign();
#endif
    auto check = [&](int64_t a, int64_t b) {
        lhs = uint64_t(a);
        rhs = uint64_t(b);
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = 0;
        dut.lhs_in = lhs;
        dut.rhs_in = rhs;
        dut.eval();
        uint64_t actual[] = {dut.divide_out, dut.exp_out, dut.inverse_out, dut.silu_out};
#else
        uint64_t actual[] = {dut.divide_out(), dut.exp_out(), dut.inverse_out(), dut.silu_out()};
#endif
        uint64_t expected[] = {
            b ? uint64_t(integer_llm::divide(a, b)) : 0,
            a <= 0 ? uint64_t(integer_llm::exp_negative(a)) : 0,
            uint64_t(integer_llm::inverse_sqrt(a)), uint64_t(integer_llm::silu(a))};
        for (unsigned operation = 0; operation < 4; ++operation) {
            if (actual[operation] != expected[operation]) {
                std::fprintf(stderr, "Q48 operation %u a=%016llx b=%016llx: got %016llx expected %016llx\n",
                    operation, (unsigned long long)lhs, (unsigned long long)rhs,
                    (unsigned long long)actual[operation], (unsigned long long)expected[operation]);
                throw std::runtime_error("Q48 RTL mismatch");
            }
        }
        ++checked;
        ++_system_clock;
    };
    constexpr int64_t one = integer_llm::one;
    const int64_t edges[] = {INT64_MIN, INT64_MAX, -49 * 195103586505167LL - 1,
        -49 * 195103586505167LL, -49 * 195103586505167LL + 1, -4 * one, -one,
        -1, 0, 1, 2, one / 16, one / 4, one - 1, one, one + 1, 4 * one};
    for (int64_t a : edges)
        for (int64_t b : edges)
            check(a, b);
    for (int range = 0; range <= 49; ++range)
        for (int delta = -1; delta <= 1; ++delta)
            check(-range * 195103586505167LL + delta, one);
    // Every normalization shift, including saturation at the smallest inputs.
    for (unsigned bit = 0; bit < 63; ++bit) {
        int64_t x = int64_t(uint64_t(1) << bit);
        for (int delta = -1; delta <= 1; ++delta)
            check(x + delta, -one);
    }
    std::mt19937_64 random(567);
    for (unsigned i = 0; i < 2048; ++i) {
        int64_t a = int64_t(random());
        int64_t b = int64_t(random());
        if ((i & 1) == 0) a = int64_t(random() % (80 * one)) - 40 * one;
        check(a, b);
    }
    std::printf("PASS: %u direct Q48 RTL vectors, four operations each\n", checked);
}
#endif
