#include "SliceCasts.cc"
#include <cstdio>
#ifdef SLICE_CASTS_RTL
#include "VSliceCasts.h"
#endif

long _system_clock = 0;

struct Harness {
    SliceCasts model;
    logic<128> data;
    logic<3> lane;
    logic<4> mode;
    logic<16> replacement;
    void _assign() {
        model.data_in = _ASSIGN(data);
        model.lane_in = _ASSIGN(lane);
        model.mode_in = _ASSIGN(mode);
        model.replacement_in = _ASSIGN(replacement);
        model._assign();
    }
};

int main() {
    Harness h;
    uint32_t words[4];
    uint32_t random = 0x13579bdf;
    h._assign();
#ifdef SLICE_CASTS_RTL
    VSliceCasts rtl;
    rtl.clk = 0;
    rtl.reset = 0;
#endif
    for (unsigned sample = 0; sample < 2048; ++sample) {
        for (unsigned word = 0; word < 4; ++word) {
            random = random*1664525u + 1013904223u;
            words[word] = sample == 0 ? 0 : sample == 1 ? UINT32_MAX : random;
            h.data.bits(word*32+31, word*32) = words[word];
#ifdef SLICE_CASTS_RTL
            rtl.data_in[word] = words[word];
#endif
        }
        h.replacement = random >> 16;
        for (unsigned lane = 0; lane < 8; ++lane) {
            h.lane = lane;
            for (unsigned mode = 0; mode < 9; ++mode) {
                h.mode = mode;
                ++_system_clock;
                const uint32_t expected = mode == 0 ? words[lane & 3]
                    : (words[lane/2] >> ((lane%2)*16)) & 0xffffu;
                if (h.model.result_out().raw != expected) {
                    std::fprintf(stderr, "C++ read mismatch: sample=%u lane=%u mode=%u\n", sample, lane, mode);
                    return 1;
                }
#ifdef SLICE_CASTS_RTL
                rtl.lane_in = lane;
                rtl.mode_in = mode;
                rtl.replacement_in = h.replacement;
                rtl.eval();
                if (rtl.result_out != expected) {
                    std::fprintf(stderr, "RTL read mismatch: sample=%u lane=%u mode=%u got=%x expected=%x\n",
                        sample, lane, mode, rtl.result_out, expected);
                    return 1;
                }
#endif
                for (unsigned word = 0; word < 4; ++word) {
                    const unsigned shift = (lane%2)*16;
                    const uint32_t edited = word != lane/2 ? words[word]
                        : (words[word] & ~(0xffffu << shift)) | (uint32_t(h.replacement) << shift);
                    if (uint32_t(h.model.edited_out().bits(word*32+31, word*32)) != edited) return 2;
#ifdef SLICE_CASTS_RTL
                    if (rtl.edited_out[word] != edited) return 3;
#endif
                }
            }
        }
    }
    std::puts("slice casts: 147456 read/write cases passed");
}
