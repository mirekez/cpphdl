#include "LazyComb.cc"
#include <cstdio>
#ifdef LAZY_VERILATOR
#include "VLazyComb.h"
#endif
long _system_clock = 0;

template<class T>
concept HasLazyTimestamp = requires(T t) { t.__prev__system_clock_value_comb; };
#ifdef SYNTHESIS
static_assert(!HasLazyTimestamp<LazyComb>);
#else
static_assert(HasLazyTimestamp<LazyComb>);
#endif

int main() {
    LazyComb dut;
    uint32_t input = 0;
#ifdef CPPHDL_STATIC
    // Legacy static bindings cannot capture a local variable.
    static uint32_t static_input = 0;
    dut.input_in = _ASSIGN(static_input);
#else
    dut.input_in = _ASSIGN(input);
#endif
    dut._assign();
#ifdef LAZY_VERILATOR
    VLazyComb rtl;
#endif
    for (unsigned sample = 0; sample < 1024; ++sample) {
        input = sample * 0x1020304u;
#ifdef CPPHDL_STATIC
        static_input = input;
#endif
        ++_system_clock;
        const uint32_t expected = input ^ 0x12345678u;
        if (dut.first_out() != expected || dut.second_out() != expected ||
            dut.first_out() != expected) return 1;
#ifndef SYNTHESIS
        if (dut.evaluations != sample + 1) return 2;
#endif
#ifdef LAZY_VERILATOR
        rtl.clk = 0;
        rtl.reset = 0;
        rtl.input_in = input;
        rtl.eval();
        if (rtl.first_out != expected || rtl.second_out != expected) return 3;
#endif
    }
    std::puts("lazy comb: 1024 samples passed");
}
