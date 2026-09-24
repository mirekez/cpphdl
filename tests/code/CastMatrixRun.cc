#include "CastMatrix.cc"
#include <cstdio>
#ifdef CAST_VERILATOR
#include "VCastMatrix.h"
#endif
long _system_clock = 0;

struct CastHarness {
    CastMatrix model;
    logic<64> input;
    logic<8> mode;
    void _assign() {
        model.value_in = _ASSIGN(input);
        model.mode_in = _ASSIGN(mode);
        model._assign();
    }
};

int main() {
    CastHarness h;
    h._assign();
#ifdef CAST_VERILATOR
    VCastMatrix dut;
#endif
    // Independent anchors: catch a common mistake shared by all spellings.
    if (h.model.as_static(0x180, 0) != 128 ||
        h.model.as_static(0x180, 1) != UINT64_C(0xffffffffffffff80) ||
        h.model.as_static(0x100, 13) != 1 ||
        h.model.as_static(0x180, 15) != UINT64_C(0xffffff80) ||
        h.model.as_static(UINT64_MAX, 16) != UINT64_C(0xffffffff) ||
        h.model.as_static(0x180, 30) != 128 ||
        h.model.as_static(1, 36) != (UINT64_C(1) << 48)) return 2;
    uint64_t rng = UINT64_C(0x853c49e6748fea9b);
    unsigned count = 0;
    for (unsigned sample = 0; sample < 2304; ++sample) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        uint64_t x = sample < 1024 ? sample : rng;
        if (sample >= 1024 && sample < 1152) x = UINT64_C(1) << (sample & 63);
        if (sample >= 1152 && sample < 1280) x = ~(UINT64_C(1) << (sample & 63));
        for (unsigned mode = 0; mode < 50; ++mode) {
            h.input = x; h.mode = mode;
            ++_system_clock;
            uint64_t expected = h.model.static_out();
            if (mode == 48 && expected !=
                (uint64_t(uint32_t((uint32_t(x) ^ 0x12345678u) + 7u)) |
                 (uint64_t(uint16_t((x >> 32) + 3u)) << 32))) return 4;
            if (mode == 49 && expected != 123) return 5;
            uint64_t a = expected, b = h.model.cstyle_out(), c = h.model.functional_out();
            if (a != b || a != c) return 3;
#ifdef CAST_VERILATOR
            dut.value_in = x; dut.mode_in = mode; dut.reset = 0; dut.clk = 0;
            dut.eval();
            a = dut.static_out; b = dut.cstyle_out; c = dut.functional_out;
#endif
            if (a != expected || b != expected || c != expected) {
                std::fprintf(stderr, "cast mode=%u input=%016llx expected=%016llx static=%016llx cstyle=%016llx functional=%016llx\n",
                    mode, (unsigned long long)x, (unsigned long long)expected,
                    (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
                return 1;
            }
            ++count;
        }
    }
    std::printf("casts: %u vectors, three cast spellings passed\n", count);
}
