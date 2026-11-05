#ifdef VERILATOR
#include "VLocalClocks.h"
using DUT = VLocalClocks;
#define READ(name) unsigned(dut.name)
#else
#include "LocalClocks.cpp"
long _system_clock = -1;
using DUT = LocalClocks;
#define READ(name) unsigned(dut.name())
#endif
#include <iostream>
#include <stdexcept>

int main() {
    DUT dut;
    unsigned fast_count = 0, slow_count = 0;
#ifndef VERILATOR
    dut._assign();
#endif
    for (unsigned tick = 0; tick < 15000; ++tick) {
        const bool fast = tick % 2 == 0, slow = tick % 5 == 0;
        const bool reset = tick < 20 || (tick >= 7000 && tick < 7030);
        if (fast) fast_count = reset ? 0 : fast_count + 1;
        if (slow) slow_count = reset ? 0 : slow_count + 1;
#ifdef VERILATOR
        dut.fast_clk = dut.slow_clk = dut.spare_clk = 0;
        dut.reset = reset; dut.eval();
        dut.fast_clk = fast; dut.slow_clk = slow; dut.spare_clk = tick % 3 == 0;
        dut.eval();
#else
        ++_system_clock;
        if (fast) dut._work_fast_clk(reset);
        if (slow) dut._work_slow_clk(reset);
        if (fast) dut._strobe_fast_clk();
        if (slow) dut._strobe_slow_clk();
        ++_system_clock;
#endif
        if (READ(a_source_out) != fast_count || READ(a_destination_out) != slow_count ||
            READ(b_source_out) != slow_count || READ(b_destination_out) != fast_count)
            throw std::runtime_error("local clock binding mismatch: " + std::to_string(tick));
    }
    std::cout << "PASS two reusable children, opposite local clock mappings, unrelated root clock, 15000 ticks\n";
}
