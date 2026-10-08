#include "SwitchControl.cpp"
#include <cstdio>
#ifdef SWITCH_GRAPH
#include "model.h"
#endif
#ifdef SWITCH_VERILATOR
#include "VSwitchControl.h"
#endif

long _system_clock = 0;
int main() {
    logic<4> mode;
    logic<3> flags;
    logic<4> selector;
    logic<8> seed;
    cpphdl_top.mode_in = _ASSIGN(mode);
    cpphdl_top.selector_in = _ASSIGN(selector);
    cpphdl_top.flags_in = _ASSIGN(flags);
    cpphdl_top.seed_in = _ASSIGN(seed);
    cpphdl_top._assign();
#ifdef SWITCH_GRAPH
    cpphdl_native::Model dut;
#endif
#ifdef SWITCH_VERILATOR
    VSwitchControl dut;
#endif
    // Hand-calculated anchors ensure that even the shared reference is checked.
    if (cpphdl_top.mixed(0, 0, 10) != 254 || cpphdl_top.mixed(0, 2, 10) != 1013 ||
        cpphdl_top.mixed(9, 0, 10) != 1034 || cpphdl_top.nested(0, 0, 10) != 35 ||
        cpphdl_top.loops(0, 0, 10) != 1276 || cpphdl_top.selector_changes(0, 0, 10) != 16 ||
        cpphdl_top.sequential(0, 0, 10) != 62 || cpphdl_top.loop_in_case(0, 0, 10) != 11 ||
        cpphdl_top.nested_mixed_returns(0, 4, 10) != 50)
        return 2;
    unsigned samples = 0;
    for (unsigned v : {0u, 1u, 127u, 255u})
        for (unsigned m = 0; m < 16; ++m)
            for (unsigned s = 0; s < 16; ++s)
                for (unsigned f = 0; f < 8; ++f) {
                    mode = m; selector = s; flags = f; seed = v;
                    bool reset = samples % 31 == 0;
                    ++_system_clock;
                    unsigned expected = cpphdl_top.result_out();
                    cpphdl_top._work(reset);
                    cpphdl_top._strobe();
#ifdef SWITCH_GRAPH
                    dut.mode[0] = m; dut.selector[0] = s; dut.flags[0] = f; dut.seed[0] = v;
                    dut.work_reset[0] = reset;
                    dut.step();
                    unsigned actual = dut.result[0], stored = dut.stored[0];
#elif defined(SWITCH_VERILATOR)
                    dut.mode_in = m; dut.selector_in = s; dut.flags_in = f; dut.seed_in = v;
                    dut.reset = reset;
                    dut.clk = 0; dut.eval();
                    dut.clk = 1; dut.eval();
                    dut.clk = 0; dut.eval();
                    unsigned actual = dut.result_out, stored = dut.stored_out;
#else
                    unsigned actual = cpphdl_top.result(), stored = cpphdl_top.stored_out();
#endif
                    if (actual != expected || stored != unsigned(cpphdl_top.stored_out())) {
                        std::fprintf(stderr, "mode=%u selector=%u flags=%u seed=%u: result=%u expected=%u stored=%u\n",
                                     m, s, f, v, actual, expected, stored);
                        return 1;
                    }
                    ++samples;
                }
    std::printf("switch control: %u exhaustive C++/backend comparisons passed\n", samples);
}
