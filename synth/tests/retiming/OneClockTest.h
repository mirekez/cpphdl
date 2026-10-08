#include <cstdio>
#include <vector>
#ifdef RETIMING_GRAPH
#include "model.h"
#define DRIVE(name, value) dut.name[0] = value
#define GET(name) dut.name[0]
#else
#include "VSynthRetiming.h"
#define DRIVE(name, value) dut.name = value
#define GET(name) dut.name
#endif
long _system_clock = 0;
int main() {
#ifdef RETIMING_GRAPH
    cpphdl_native::Model dut;
#else
    VSynthRetiming dut;
#endif
    uint8_t input = 0;
    cpphdl_top.a_in = _ASSIGN(input);
    std::vector<unsigned> expected;
    unsigned age = 0;
    for (unsigned cycle = 0; cycle < 2000; ++cycle) {
        bool reset = cycle % 101 == 0;
        input = uint8_t(cycle * 37);
        cpphdl_top._work(reset); cpphdl_top._strobe();
        expected.push_back(uint8_t(cpphdl_top.result_out()));
        DRIVE(a, input); DRIVE(work_reset, reset);
#ifdef RETIMING_GRAPH
        dut.step();
#else
        dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#endif
        age = reset ? 0 : age + 1;
        if (reset && GET(result) != 0) return 1;
        if (age > RETIMING_LATENCY && GET(result) != expected[cycle - RETIMING_LATENCY]) return 2;
        ++_system_clock;
    }
    std::puts("2000 transactions: repeated one-clock calls, pipeline and reset passed");
}
