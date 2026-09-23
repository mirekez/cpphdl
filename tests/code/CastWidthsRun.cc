#include "CastWidths.cc"
#include <cstdio>
#ifdef CAST_VERILATOR
#include "VCastWidths.h"
#endif
#ifndef CAST_WIDTH
#define CAST_WIDTH 9
#endif
long _system_clock = 0;
struct WidthHarness {
    CastWidths<CAST_WIDTH> model;
    logic<64> value;
    void _assign() {
        model.value_in = _ASSIGN(value);
        model._assign();
    }
};
int main() {
    WidthHarness h;
    h._assign();
#ifdef CAST_VERILATOR
    VCastWidths dut;
#endif
    constexpr uint64_t mask = (UINT64_C(1) << CAST_WIDTH) - 1;
    uint64_t x = UINT64_C(0x123456789abcdef0);
    for (unsigned n = 0; n < 4096; ++n) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        h.value = x;
        ++_system_clock;
        uint64_t expected = (x & mask) ^ (((x >> 7) & mask) << 1) ^ (((x >> 13) & mask) << 2);
        uint64_t actual = h.model.result_out();
        if (actual != expected) return 1;
#ifdef CAST_VERILATOR
        dut.value_in = x; dut.reset = 0; dut.clk = 0; dut.eval();
        actual = dut.result_out;
#endif
        if (actual != expected) {
            std::fprintf(stderr, "WIDTH=%u x=%llx actual=%llx expected=%llx\n", CAST_WIDTH,
                (unsigned long long)x, (unsigned long long)actual, (unsigned long long)expected);
            return 1;
        }
    }
    std::printf("parameterized casts: WIDTH=%u passed\n", CAST_WIDTH);
}
