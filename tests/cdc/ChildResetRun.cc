#ifdef VERILATOR
#include "VChildReset.h"
#include <verilated.h>
using Device = VChildReset;
#else
#include "ChildReset.cpp"
long _system_clock = -1;
using Device = ChildReset;
#endif
#include <array>
#include <iostream>
#include <stdexcept>

int main() {
    Device device;
    bool clear = false;
    unsigned mask = 0, scalar = 0;
    std::array<unsigned, 4> expected{};
#ifndef VERILATOR
    device.clear_in = _ASSIGN(clear);
    device.clear_mask_in = _ASSIGN(mask);
    device._assign();
#endif
    for (unsigned cycle = 0; cycle < 2000; ++cycle) {
        const bool reset = cycle == 0 || cycle == 911;
        clear = cycle % 37 == 4;
        mask = (cycle * 83) % 191 < 16 ? (cycle * 83) % 16 : 0;
        scalar = reset || clear ? 0 : (scalar + 1) & 255;
        for (unsigned i = 0; i < 4; ++i)
            expected[i] = reset || ((mask >> i) & 1) ? 0 : (expected[i] + 1) & 255;
#ifdef VERILATOR
        device.clk = 0;
        device.reset = reset;
        device.clear_in = clear;
        device.clear_mask_in = mask;
        device.eval();
        device.clk = 1;
        device.eval();
        const unsigned actual = device.scalar_out;
#else
        ++_system_clock;
        device._work(reset);
        device._strobe();
        const unsigned actual = unsigned(device.scalar_out());
#endif
        if (actual != scalar)
            throw std::runtime_error("scalar child reset mismatch at clock " + std::to_string(cycle));
        for (unsigned i = 0; i < 4; ++i) {
#ifdef VERILATOR
            const unsigned value = device.array_out[i];
#else
            const unsigned value = unsigned(device.array_out[i]());
#endif
            if (value != expected[i])
                throw std::runtime_error("indexed child reset mismatch at clock " + std::to_string(cycle));
        }
    }
    std::cout << "PASS scalar and indexed child synchronous reset, 2000 clocks\n";
}
