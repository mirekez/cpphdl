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
        // Exhaust all signed-byte encodings before the random vectors.
        if (n < 256) x = n;
        h.value = x;
        ++_system_clock;
        uint64_t expected = (x & mask) ^ (((x >> 7) & mask) << 1) ^ (((x >> 13) & mask) << 2);
        uint64_t actual = h.model.result_out();
        uint64_t signed_expected = ((x & 0xff) |
            ((x & 0x80) ? UINT64_C(0xffffffffffffff00) : 0)) & mask;
        uint64_t a = h.model.signed_static_out();
        uint64_t b = h.model.signed_cstyle_out();
        uint64_t c = h.model.signed_functional_out();
        constexpr uint64_t strobe_expected = (UINT64_C(1) << ((CAST_WIDTH + 7) / 8)) - 1;
        uint64_t strobe = h.model.strobe_out();
        if (strobe != strobe_expected) return 3;
        uint64_t inverted = h.model.inverted_out();
        if (inverted != ((~x) & mask)) return 4;
        if (actual != expected) return 1;
        if (a != signed_expected || b != signed_expected || c != signed_expected) {
            std::fprintf(stderr, "C++ sign extension WIDTH=%u x=%llx expected=%llx static=%llx cstyle=%llx functional=%llx\n",
                CAST_WIDTH, (unsigned long long)x, (unsigned long long)signed_expected,
                (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
            return 2;
        }
#ifdef CAST_VERILATOR
        dut.value_in = x; dut.reset = 0; dut.clk = 0; dut.eval();
        actual = dut.result_out;
        a = dut.signed_static_out; b = dut.signed_cstyle_out; c = dut.signed_functional_out;
        strobe = dut.strobe_out;
        inverted = dut.inverted_out;
#endif
        if (inverted != ((~x) & mask)) {
            std::fprintf(stderr, "complement WIDTH=%u input=%llx actual=%llx expected=%llx\n",
                CAST_WIDTH, (unsigned long long)x, (unsigned long long)inverted,
                (unsigned long long)((~x) & mask));
            return 4;
        }
        if (strobe != strobe_expected) {
            std::fprintf(stderr, "compound cast WIDTH=%u strobe=%llx expected=%llx\n",
                CAST_WIDTH, (unsigned long long)strobe, (unsigned long long)strobe_expected);
            return 3;
        }
        if (a != signed_expected || b != signed_expected || c != signed_expected) {
            std::fprintf(stderr, "RTL sign extension WIDTH=%u x=%llx expected=%llx static=%llx cstyle=%llx functional=%llx\n",
                CAST_WIDTH, (unsigned long long)x, (unsigned long long)signed_expected,
                (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
            return 2;
        }
        if (actual != expected) {
            std::fprintf(stderr, "WIDTH=%u x=%llx actual=%llx expected=%llx\n", CAST_WIDTH,
                (unsigned long long)x, (unsigned long long)actual, (unsigned long long)expected);
            return 1;
        }
    }
    std::printf("parameterized casts: WIDTH=%u passed\n", CAST_WIDTH);
}
