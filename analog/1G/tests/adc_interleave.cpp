#ifdef VERILATOR
#include "VInterleavedSARController.h"
#include <verilated.h>
using Device = VInterleavedSARController;
#else
#include "../adc_interleave.hpp"
long _system_clock = -1;
using Device = InterleavedSARController;
#endif
#include <array>
#include <algorithm>
#include <bitset>
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>

struct Expected {
    unsigned due, slice, code;
};

int main(int argc, char** argv) {
    try {
        Device device;
        bool enable = true, launch = true;
        unsigned comparisons = 0;
#ifndef VERILATOR
        device.enable_in = _ASSIGN(enable);
        device.sample_enable_in = _ASSIGN(launch);
        device.comparisons_in = _ASSIGN(comparisons);
        device._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        std::array<unsigned, 16> held{}, trial{};
        std::array<bool, 16> acquiring{};
        std::deque<Expected> expected;
        std::bitset<256> codes;
        std::bitset<16> slices;
        unsigned position = 0, completed = 0, consecutive = 0, longest = 0;
        auto input = [](unsigned time) { return (time * 83 + (time / 61) * 53) & 255; };
        for (unsigned cycle = 0; cycle < 12000; ++cycle) {
            const bool reset = cycle == 0 || cycle == 305 || cycle == 7999;
            enable = !(cycle >= 1700 && cycle < 1707) && !(cycle >= 8100 && cycle < 8102);
            launch = !(cycle >= 4400 && cycle < 4441) && !(cycle >= 9000 && cycle < 9100);
            comparisons = 0;
            for (unsigned i = 0; i < 16; ++i)
                comparisons |= unsigned(held[i] >= trial[i]) << i;
            if (reset || !enable) {
                position = 0;
                expected.clear();
                acquiring.fill(false);
            } else {
                if (launch)
                    expected.push_back({cycle + 13, position, input(cycle + 3)});
                position = (position + 1) & 15;
            }
#ifdef VERILATOR
            device.clk = 0;
            device.reset = reset;
            device.enable_in = enable;
            device.sample_enable_in = launch;
            device.comparisons_in = comparisons;
            device.eval();
            device.clk = 1;
            device.eval();
#define READ_PORT(name) unsigned(device.name)
#define READ_ARRAY(name, i) unsigned(device.name[i])
#else
            ++_system_clock;
            device._work(reset);
            device._strobe();
#define READ_PORT(name) unsigned(device.name())
#define READ_ARRAY(name, i) unsigned(device.name[i]())
#endif
            for (unsigned i = 0; i < 16; ++i) {
                const bool acquire = READ_ARRAY(acquire_out, i);
                if (acquiring[i] && !acquire && !reset && enable)
                    held[i] = input(cycle);
                acquiring[i] = acquire;
                trial[i] = READ_ARRAY(trial_out, i);
            }
            const bool valid = READ_PORT(valid_out);
            if (READ_PORT(fault_out))
                throw std::runtime_error("valid interleaved schedule collided with a busy slice");
            const bool due = !expected.empty() && expected.front().due == cycle;
            if (valid != due)
                throw std::runtime_error("interleaved output cadence/flush differs at clock " + std::to_string(cycle));
            if (due) {
                const auto result = expected.front();
                expected.pop_front();
                if (READ_PORT(data_out) != result.code || READ_PORT(slice_out) != result.slice)
                    throw std::runtime_error("interleaved sample order/value differs at clock " + std::to_string(cycle));
                codes.set(result.code);
                slices.set(result.slice);
                ++completed;
                longest = std::max(longest, ++consecutive);
            } else {
                consecutive = 0;
            }
            if (trace) {
                trace << cycle << ',' << READ_PORT(data_out) << ',' << READ_PORT(slice_out) << ',' << valid;
                for (unsigned i = 0; i < 16; ++i)
                    trace << ',' << trial[i] << ',' << acquiring[i] << ',' << READ_ARRAY(convert_out, i);
                trace << '\n';
            }
#undef READ_PORT
#undef READ_ARRAY
        }
        if (codes.count() != 256 || slices.count() != 16 || completed < 11000 || longest < 3000)
            throw std::runtime_error("interleaving test missed full code, slice or sustained-throughput coverage");
        std::cout << "12000 clocks; " << completed << " conversions; all 256 codes and 16 slices, sustained "
                     "one-result-per-reference-clock throughput, launch gaps, reset and disable flushing passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
