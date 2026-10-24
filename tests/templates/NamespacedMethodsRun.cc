#ifdef VERILATOR
#include "VNamespacedMethods.h"
using Device = VNamespacedMethods;
#else
#include "NamespacedMethods.h"
long _system_clock = -1;
using Device = NamespacedMethods;
#endif
#include <cstdio>

int main() {
    Device dut;
    uint32_t input = 0;
#ifndef VERILATOR
    dut.value_in = _ASSIGN(input);
    dut._assign();
#endif
    for (unsigned cycle = 0; cycle < 600; ++cycle) {
        input = cycle * 7919u;
        bool reset = cycle % 31 == 0;
#ifdef VERILATOR
        dut.value_in = input; dut.reset = reset;
        dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval();
        uint32_t actual = dut.value_out;
#else
        dut._work(reset); dut._strobe(); ++_system_clock;
        uint32_t actual = dut.value_out();
#endif
        uint32_t expected = reset ? 0 : (input + 5u) + (input ^ 17u);
        if (actual != expected) {
            std::fprintf(stderr, "cycle %u: got %u expected %u\n", cycle, actual, expected);
            return 1;
        }
    }
    std::puts("PASS namespaced HLS records, module methods and inherited template helpers");
}
