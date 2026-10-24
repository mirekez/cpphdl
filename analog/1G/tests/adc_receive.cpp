#ifdef VERILATOR
#include "VADCReceiveController.h"
#include <verilated.h>
using Device = VADCReceiveController;
#else
#include "../adc_receive.hpp"
long _system_clock = -1;
using Device = ADCReceiveController;
#endif
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>

struct Conversion { unsigned due, slice, code; };
struct Normalized { unsigned due, slice, code; bool clipped; };

int main(int argc, char** argv) {
    try {
        Device device;
        bool enable = true, launch = true;
        unsigned comparisons = 0, offset = 128, gain = 424;
#ifndef VERILATOR
        device.enable_in = _ASSIGN(enable);
        device.sample_enable_in = _ASSIGN(launch);
        device.comparisons_in = _ASSIGN(comparisons);
        device.offset_in = _ASSIGN(offset);
        device.gain_in = _ASSIGN(gain);
        device._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        std::array<unsigned, 16> held{}, trial{};
        std::array<bool, 16> acquiring{};
        std::deque<Conversion> raw;
        std::deque<Normalized> normalized;
        std::bitset<256> codes;
        std::bitset<16> slices;
        unsigned position = 0, completed = 0, clipped = 0, previous_code = 128;
        unsigned previous_raw = 0, previous_slice = 0;
        bool previous_valid = false;
        auto input = [](unsigned time) { return (time * 83 + (time / 61) * 53) & 255; };
        for (unsigned cycle = 0; cycle < 12000; ++cycle) {
            const bool reset = cycle == 0 || cycle == 305 || cycle == 7999;
            enable = !(cycle >= 1700 && cycle < 1707) && !(cycle >= 8100 && cycle < 8102);
            launch = !(cycle >= 4400 && cycle < 4441) && !(cycle >= 9000 && cycle < 9100);
            offset = std::array<unsigned, 5>{0, 64, 128, 192, 255}[(cycle / 17) % 5];
            gain = std::array<unsigned, 8>{0, 1, 128, 256, 424, 1024, 32768, 65535}[(cycle / 31) % 8];
            comparisons = 0;
            for (unsigned i = 0; i < 16; ++i)
                comparisons |= unsigned(held[i] >= trial[i]) << i;
            if (reset || !enable) {
                raw.clear();
                normalized.clear();
                acquiring.fill(false);
                position = 0;
                previous_code = 128;
                previous_raw = 0;
                previous_slice = 0;
                previous_valid = false;
            } else {
                // The inter-module register boundary adds one cycle before the
                // normalizer's seven-cycle acceptance-to-output latency.
                if (previous_valid) {
                    const long value = 128 + std::lround((double(previous_raw) - offset) * gain / 256);
                    normalized.push_back({cycle + 7, previous_slice, unsigned(std::clamp(value, 0L, 255L)),
                                          value < 0 || value > 255});
                }
                if (launch)
                    raw.push_back({cycle + 13, position, input(cycle + 3)});
                position = (position + 1) & 15;
            }
#ifdef VERILATOR
            device.clk = 0;
            device.reset = reset;
            device.enable_in = enable;
            device.sample_enable_in = launch;
            device.comparisons_in = comparisons;
            device.offset_in = offset;
            device.gain_in = gain;
            device.eval();
            device.clk = 1;
            device.eval();
#define READ(name) unsigned(device.name)
#define ARRAY(name, i) unsigned(device.name[i])
#else
            ++_system_clock;
            device._work(reset);
            device._strobe();
#define READ(name) unsigned(device.name())
#define ARRAY(name, i) unsigned(device.name[i]())
#endif
            for (unsigned i = 0; i < 16; ++i) {
                const bool acquire = ARRAY(acquire_out, i);
                if (acquiring[i] && !acquire && !reset && enable)
                    held[i] = input(cycle);
                acquiring[i] = acquire;
                trial[i] = ARRAY(trial_out, i);
            }
            const bool due = !raw.empty() && raw.front().due == cycle;
            if (READ(fault_out) || bool(READ(valid_out)) != due)
                throw std::runtime_error("SAR receive raw cadence or fault at " + std::to_string(cycle));
            if (due) {
                previous_raw = raw.front().code;
                previous_slice = raw.front().slice;
                raw.pop_front();
            }
            previous_valid = due;
            if (READ(data_out) != previous_raw || READ(slice_out) != previous_slice)
                throw std::runtime_error("SAR receive raw code/slice at " + std::to_string(cycle));
            const bool ready = !normalized.empty() && normalized.front().due == cycle;
            if (bool(READ(normalized_valid_out)) != ready || (!ready && READ(clipped_out)))
                throw std::runtime_error("SAR receive normalized validity at " + std::to_string(cycle));
            if (ready) {
                const auto expected = normalized.front();
                normalized.pop_front();
                if (READ(normalized_slice_out) != expected.slice || bool(READ(clipped_out)) != expected.clipped)
                    throw std::runtime_error("SAR receive slice/clipping alignment at " + std::to_string(cycle));
                previous_code = expected.code;
                codes.set(previous_raw);
                slices.set(expected.slice);
                clipped += expected.clipped;
                ++completed;
            }
            if (READ(normalized_out) != previous_code)
                throw std::runtime_error("SAR receive calibration/sample at " + std::to_string(cycle));
            if (trace) {
                trace << cycle << ',' << READ(data_out) << ',' << READ(slice_out) << ',' << READ(valid_out)
                      << ',' << READ(normalized_out) << ',' << READ(normalized_slice_out)
                      << ',' << READ(normalized_valid_out) << ',' << READ(clipped_out) << ',' << READ(fault_out);
                for (unsigned i = 0; i < 16; ++i)
                    trace << ',' << trial[i] << ',' << acquiring[i] << ',' << ARRAY(convert_out, i);
                trace << '\n';
            }
#undef READ
#undef ARRAY
        }
        if (completed < 11000 || clipped < 1000 || codes.count() != 256 || slices.count() != 16)
            throw std::runtime_error("insufficient composed ADC receive coverage");
        std::cout << "PASS 12000 clocks; " << completed << " calibrated conversions; all codes/slices, "
                     "changing calibration, signed rounding, clipping, gaps and in-flight flushes\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
