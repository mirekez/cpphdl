#include "EmptyInit.cc"
#include <cstdio>
#ifdef EMPTY_INIT_VERILATOR
#include "VEmptyInit.h"
#endif

long _system_clock = 0;

struct Harness {
    EmptyInit model;
    logic<16> seed;
    logic<5> mode;
    void _assign() {
        model.seed_in = _ASSIGN(seed);
        model.mode_in = _ASSIGN(mode);
    }
};

static uint64_t expected(uint16_t seed, unsigned mode) {
    switch (mode) {
    case 0: case 1: case 2: case 3: case 4: case 11: return 0;
    case 5: case 6: case 14: return 0x864235;
    case 7: case 13: return seed & 0xff;
    case 8: case 15: return 0x5a000000 | (uint64_t(seed) << 8) | (seed & 0xff);
    case 9: return 0xa7864235;
    case 10: return 0x864200 | (seed & 0xff);
    case 12: return 0x5a000000;
    case 16: return 0xa5a50000u | seed;
    case 17: return (uint64_t(0x5a5a) << 24) | (uint64_t(seed) << 8) | (seed & 0xff);
    case 18: return 0x12340000u | seed;
    case 19: return 0x56780000u | seed;
    case 20: return uint64_t(0x55aa) << 24;
    case 21: return 0;
    default: return (uint64_t(seed) << 8) | (seed & 0xff);
    }
}

int main() {
    Harness h;
    h._assign();
#ifdef EMPTY_INIT_VERILATOR
    VEmptyInit rtl;
    rtl.clk = 0;
    rtl.reset = 0;
#endif
    // Exhaust the input and alternate populated/cleared aggregates. The
    // independent field-value oracle does not depend on C++ struct padding.
    for (unsigned seed = 0; seed < 65536; ++seed) {
        for (unsigned mode = 0; mode < 22; ++mode) {
            h.seed = seed;
            h.mode = mode;
            ++_system_clock;
            const uint64_t want = expected(seed, mode);
            const uint64_t native = h.model.result_out();
            if (native != want) {
                std::fprintf(stderr, "C++ seed=%u mode=%u: %llx != %llx\n", seed, mode,
                    (unsigned long long)native, (unsigned long long)want);
                return 1;
            }
#ifdef EMPTY_INIT_VERILATOR
            rtl.seed_in = seed;
            rtl.mode_in = mode;
            rtl.eval();
            if (rtl.result_out != want) {
                std::fprintf(stderr, "RTL seed=%u mode=%u: %llx != %llx\n", seed, mode,
                    (unsigned long long)rtl.result_out, (unsigned long long)want);
                return 1;
            }
#endif
        }
    }
    std::puts("empty/partial/nested/union/inherited initialization: 1441792 checks passed");
}
