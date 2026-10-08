#include "SwitchCompleteCases.cc"
#include <cstdio>
#ifdef COMPLETE_GRAPH
#include "model.h"
#endif
#ifdef COMPLETE_RTL
#include "VSwitchCompleteCases.h"
#endif
long _system_clock = 0;
SwitchCompleteCases complete_top;
int main() {
    cpphdl::logic<2> selector;
    cpphdl::logic<1> flag;
    cpphdl::logic<8> seed;
    complete_top.selector_in = _ASSIGN(selector);
    complete_top.flag_in = _ASSIGN(flag);
    complete_top.seed_in = _ASSIGN(seed);
    complete_top._assign();
#ifdef COMPLETE_GRAPH
    cpphdl_native::Model dut;
#endif
#ifdef COMPLETE_RTL
    VSwitchCompleteCases dut;
#endif
    unsigned samples = 0;
    for (unsigned v = 0; v < 256; ++v)
        for (unsigned s = 0; s < 4; ++s)
            for (unsigned f = 0; f < 2; ++f) {
                unsigned expected_bytes[] = {
                    s < 2 ? v + (f ? 1 : 2) : v + (s == 2 ? 2 : 3),
                    s == 0 ? v : v + (s == 2 ? 6 : 5),
                    v + (s == 0 ? (f ? 1 : 2) : s == 1 ? (f ? 4 : 3) : 5),
                    v + (s == 0 ? 11 : s + 1),
                    s == 0 ? v + 1 : s == 1 ? v + (f ? 3 : 12) : v + 10,
                    (v & 240) | ((v + (s < 2 ? s : 2)) & 15),
                    v + (s < 2 ? s + 1 : 0),
                    (v + (s == 2 ? 1 : s == 1 ? unsigned(-1) : 0)) & 3
                };
                uint64_t expected = 0;
                for (unsigned i = 0; i < 8; ++i) expected |= uint64_t(expected_bytes[i] & 255) << (8 * i);
                selector = s; flag = f; seed = v;
                ++_system_clock;
                uint64_t native = complete_top.result_out();
                uint64_t actual = native;
#ifdef COMPLETE_GRAPH
                dut.selector[0] = s; dut.flag[0] = f; dut.seed[0] = v;
                dut.eval(false);
                actual = uint64_t(dut.result[0]) | (uint64_t(dut.result[1]) << 32);
#endif
#ifdef COMPLETE_RTL
                dut.selector_in = s; dut.flag_in = f; dut.seed_in = v;
                dut.eval();
                actual = dut.result_out;
#endif
                if (native != expected || actual != expected) {
                    std::fprintf(stderr, "s=%u f=%u seed=%u: cpp=%llx backend=%llx expected=%llx\n",
                                 s, f, v, (unsigned long long)native, (unsigned long long)actual,
                                 (unsigned long long)expected);
                    return 1;
                }
                ++samples;
            }
    std::printf("complete switch: %u oracle comparisons passed\n", samples);
}
