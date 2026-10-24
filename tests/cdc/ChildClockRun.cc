#ifdef VERILATOR
#include "VChildClock.h"
#include <verilated.h>
using Device = VChildClock;
#else
#include "ChildClock.cpp"
long _system_clock = -1;
using Device = ChildClock;
#endif
#include <iostream>
#include <stdexcept>

int main() {
    Device device;
    bool clear = false, old_fast = false, old_slow = false;
    unsigned fast_count = 0, negative_count = 0, slow_count = 0;
#ifndef VERILATOR
    device.clear_in = _ASSIGN(clear);
    device._assign();
#else
    device.fast_clk = device.slow_clk = 0;
    device.reset = 1;
    device.clear_in = 0;
    device.eval();
#endif
    for (unsigned time = 0; time < 12000; ++time) {
        const bool fast = (time / 2) & 1, slow = (time / 5) & 1;
        const bool reset = time < 30 || (time >= 4020 && time < 4060);
        clear = time % 137 < 5;
        const auto advance = [&](unsigned value) { return reset || clear ? 0u : (value + 1) & 255; };
        if (fast && !old_fast)
            fast_count = advance(fast_count);
        if (!fast && old_fast)
            negative_count = advance(negative_count);
        if (slow && !old_slow)
            slow_count = advance(slow_count);
#ifdef VERILATOR
        device.reset = reset;
        device.clear_in = clear;
        device.fast_clk = fast;
        device.slow_clk = slow;
        device.eval();
#define READ(name) unsigned(device.name)
#else
        ++_system_clock;
        if (fast && !old_fast) {
            device._work_fast_clk(reset);
            device._strobe_fast_clk();
        }
        if (!fast && old_fast) {
            device._work_neg_fast_clk(reset);
            device._strobe_neg_fast_clk();
        }
        if (slow && !old_slow) {
            device._work_slow_clk(reset);
            device._strobe_slow_clk();
        }
#define READ(name) unsigned(device.name())
#endif
        if (READ(fast_out) != fast_count || READ(negative_out) != negative_count
            || READ(slow_out[0]) != slow_count || READ(slow_out[1]) != slow_count)
            throw std::runtime_error("child clock/edge/reset mismatch at tick " + std::to_string(time));
#undef READ
        old_fast = fast;
        old_slow = slow;
    }
    std::cout << "PASS scalar, indexed, delegated and negative-edge child clocks; 12000 ticks\n";
}
