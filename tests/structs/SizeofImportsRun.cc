#include "SizeofImports.cc"
#include <cstdio>
#ifdef SIZEOF_IMPORTS_VERILATOR
#include "VSizeofImports.h"
#endif

long _system_clock = 0;

struct Harness {
    SizeofImports model;
    logic<16> seed;
    logic<2> mode;
    void _assign() {
        model.seed_in = _ASSIGN(seed);
        model.mode_in = _ASSIGN(mode);
        model._assign();
    }
};

int main() {
    static_assert(sizeof(SizeofTypes::Word) == 8);
    static_assert(sizeof(SizeofTypes::Nested) == 16);
    static_assert(sizeof(SizeofTypes::ParentOnly) == 24);
    static_assert(sizeof(SizeofTypes::Bytes<3>) == 3);
    static_assert(sizeof(SizeofTypes::Bytes<5>) == 5);
    Harness h;
    h._assign();
#ifdef SIZEOF_IMPORTS_VERILATOR
    VSizeofImports rtl;
    rtl.clk = 0;
    rtl.reset = 0;
#endif
    for (unsigned seed = 0; seed < 65536; ++seed) {
        const uint64_t expected[] = {
            0x020808 | (uint64_t(seed) << 32), 0x030503, 0x100808,
            (uint64_t(24) << 32) | (24 + uint64_t(seed))
        };
        for (unsigned mode = 0; mode < 4; ++mode) {
            h.seed = seed;
            h.mode = mode;
            ++_system_clock;
            const uint64_t native = h.model.result_out();
            if (native != expected[mode]) {
                std::fprintf(stderr, "C++ sizeof mismatch seed=%u mode=%u: %llx != %llx\n",
                    seed, mode, (unsigned long long)native, (unsigned long long)expected[mode]);
                return 1;
            }
#ifdef SIZEOF_IMPORTS_VERILATOR
            rtl.seed_in = seed;
            rtl.mode_in = mode;
            rtl.eval();
            if (rtl.result_out != expected[mode]) {
                std::fprintf(stderr, "RTL sizeof mismatch seed=%u mode=%u: %llx != %llx\n",
                    seed, mode, (unsigned long long)rtl.result_out, (unsigned long long)expected[mode]);
                return 1;
            }
#endif
        }
    }
    std::puts("sizeof-only package dependencies: 262144 checks passed");
}
