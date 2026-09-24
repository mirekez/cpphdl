#include "CastMatrix.cc"
#include <cstdio>
#include <cstdlib>
#ifdef CAST_VERILATOR
#include "VCastMatrix.h"
#endif
long _system_clock = 0;

// Independent bit-pattern oracle: no signed casts or signed shifts shared
// with the DUT. In particular, widening to an unsigned destination must still
// extend the sign of a signed source, before any destination truncation.
static uint64_t sign_bits(uint64_t x, unsigned width) {
    const uint64_t mask = (UINT64_C(1) << width) - 1;
    return (x & mask) | ((x & (UINT64_C(1) << (width - 1))) ? ~mask : 0);
}

static uint64_t sign_expected(uint64_t x, unsigned mode) {
    switch (mode) {
    case 50: case 51: case 59: case 60: return sign_bits(x, 8);
    case 52: return sign_bits(x, 8) & 0xffff;
    case 53: case 65: return x & 0xff;
    case 54: return (x & 0x100) ? sign_bits(x, 8) : x & 0xff;
    case 55: return ((x & 0x100) ? sign_bits(x, 8) : x) & 0xffffffff;
    case 56: return sign_bits(x, (x & 0x100) ? 8 : 16);
    case 57: return (sign_bits(x, 16) >> 3) |
        ((x & 0x8000) ? UINT64_C(0xe000000000000000) : 0);
    case 58: return sign_bits(x, 16) + 1;
    case 61: return (x & 0x80) != 0;
    case 62: case 63: return sign_bits(x, 32);
    case 64: return (x & UINT64_C(0x80000000)) ? UINT64_MAX : 0;
    case 66: return x;
    // The addition wraps at 32 bits BEFORE widening for modulo/comparison.
    case 67: return ((x + 1) & UINT64_C(0xffffffff)) % 3;
    case 68: return ((x + 1) & UINT64_C(0xffffffff)) < 3;
    case 69: return (x & UINT64_C(0x80000000)) && (x & UINT64_C(0xffffffff)) != UINT64_C(0xffffffff);
    case 70: return (sign_bits(x, 32) >> 1) |
        ((x & UINT64_C(0x80000000)) ? UINT64_C(0x8000000000000000) : 0);
    case 71: case 72: return sign_bits(x, 8) & 0xffff;
    case 73: return x != 0;
    case 74: return (x >> 8) & 0xffffff;
    case 75: return x & 0xffff;
    case 76: return (x & ~UINT64_C(0xffff00)) | ((x & 0xffff) << 8);
    case 77: return (~x) & 0xff;
    default: std::abort();
    }
}

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
    constexpr uint64_t boundaries[] = {
        0, 1, 0x7e, 0x7f, 0x80, 0x81, 0xff, 0x100,
        0x7ffe, 0x7fff, 0x8000, 0x8001, 0xffff, 0x10000,
        UINT64_C(0x7ffffffe), UINT64_C(0x7fffffff),
        UINT64_C(0x80000000), UINT64_C(0x80000001),
        UINT64_C(0xffffffff), UINT64_C(0x100000000),
        UINT64_C(0x7ffffffffffffffe), UINT64_C(0x7fffffffffffffff),
        UINT64_C(0x8000000000000000), UINT64_C(0x8000000000000001),
        UINT64_MAX - 1, UINT64_MAX
    };
    constexpr unsigned basic_samples = 2304 + sizeof(boundaries) / sizeof(boundaries[0]);
    // Retain the full existing cast matrix, then exhaust every 16-bit input
    // for the sign-extension cases, including both arms of each conditional.
    for (unsigned sample = 0; sample < basic_samples + 65536; ++sample) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        uint64_t x = sample < 1024 ? sample : rng;
        if (sample >= 1024 && sample < 1152) x = UINT64_C(1) << (sample & 63);
        if (sample >= 1152 && sample < 1280) x = ~(UINT64_C(1) << (sample & 63));
        if (sample >= 2304 && sample < basic_samples) x = boundaries[sample - 2304];
        if (sample >= basic_samples) x = sample - basic_samples;
        for (unsigned mode = sample < basic_samples ? 0 : 50; mode < 78; ++mode) {
            h.input = x; h.mode = mode;
            ++_system_clock;
            uint64_t expected = h.model.static_out();
            if (mode >= 50) expected = sign_expected(x, mode);
            if (mode == 48 && expected !=
                (uint64_t(uint32_t((uint32_t(x) ^ 0x12345678u) + 7u)) |
                 (uint64_t(uint16_t((x >> 32) + 3u)) << 32))) return 4;
            if (mode == 49 && expected != 123) return 5;
            uint64_t a = h.model.static_out(), b = h.model.cstyle_out(), c = h.model.functional_out();
            if (a != expected || b != expected || c != expected) {
                std::fprintf(stderr, "C++ cast mode=%u input=%016llx expected=%016llx static=%016llx cstyle=%016llx functional=%016llx\n",
                    mode, (unsigned long long)x, (unsigned long long)expected,
                    (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
                return 3;
            }
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
