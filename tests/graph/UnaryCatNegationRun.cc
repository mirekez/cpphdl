#include "UnaryCatNegation.cc"
#include <cstdio>
#ifdef NEGATE_GRAPH
#include "model.h"
#endif
#ifdef NEGATE_RTL
#include "VNegateProbe.h"
#endif

long _system_clock = 0;

int main() {
    NegateProbe dut;
    cpphdl::logic<8> input;
    cpphdl::logic<8> right;
    cpphdl::logic<64> wide;
    cpphdl::logic<5> mode;
    dut.data_in = _ASSIGN(input);
    dut.right_in = _ASSIGN(right);
    dut.wide_in = _ASSIGN(wide);
    dut.mode_in = _ASSIGN(mode);
    dut._assign();
#ifdef NEGATE_GRAPH
    cpphdl_native::Model graph;
#endif
#ifdef NEGATE_RTL
    VNegateProbe rtl;
    rtl.clk = 0;
    rtl.reset = 0;
#endif
    unsigned samples = 0;
    uint64_t random = 0x8abc0123def04567ULL;
    const uint64_t edges[] = {0, 1, 255, 256, 0xffffffffULL,
                             0x100000000ULL, 0x8000000000000000ULL, ~uint64_t(0)};
    for (unsigned sample = 0; sample < 1024; ++sample) {
      random ^= random << 13; random ^= random >> 7; random ^= random << 17;
      for (unsigned op = 0; op < 18; ++op) {
        uint64_t a = sample & 255;
        uint64_t b = (sample * 73 + (sample >> 8)) & 255;
        uint64_t w = sample < 8 ? edges[sample] : random;
        uint64_t expected_wide[] = {
            0 - a, 0 - (256 + a), 0 - w, a - b, b - a, a - b,
            0 - a, a, (~a) & 255, a == 0, 256 + a,
            (uint64_t(1) << 56) | ((0 - (256 + a)) & 0x00ffffffffffffffULL),
            (a - b) & 255, (a + b) & 255, (~a) & 511, a == 0,
            (((0 - a) >> 8) & 255) << 8, (0 - a) >> 63
        };
        input = sample;
        right = b; wide = w; mode = op;
        ++_system_clock;
        unsigned expected = (0u - sample) & 255;
        unsigned actual = uint64_t(dut.value_out()) & 255;
        uint64_t actual_wide = dut.result64_out();
        if (actual != expected || actual_wide != expected_wide[op]) {
            std::fprintf(stderr, "C++ sample=%u op=%u: %llx expected=%llx\n", sample, op,
                         (unsigned long long)actual_wide, (unsigned long long)expected_wide[op]);
            return 1;
        }
#ifdef NEGATE_GRAPH
        graph.data[0] = a; graph.right[0] = b; graph.mode[0] = op;
        graph.wide[0] = uint32_t(w); graph.wide[1] = uint32_t(w >> 32);
        graph.eval(false);
        if (graph.value[0] != expected) return 2;
        actual_wide = uint64_t(graph.result64[0]) | (uint64_t(graph.result64[1]) << 32);
#endif
#ifdef NEGATE_RTL
        rtl.data_in = a; rtl.right_in = b; rtl.wide_in = w; rtl.mode_in = op;
        rtl.eval();
        if (rtl.value_out != expected) return 2;
        actual_wide = rtl.result64_out;
#endif
        if (actual_wide != expected_wide[op]) {
            std::fprintf(stderr, "backend sample=%u op=%u: %llx expected=%llx\n", sample, op,
                         (unsigned long long)actual_wide, (unsigned long long)expected_wide[op]);
            return 3;
        }
        ++samples;
      }
    }
    std::printf("unary operators: %u oracle cases passed\n", samples);
}
