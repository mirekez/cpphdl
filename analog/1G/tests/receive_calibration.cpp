#ifdef VERILATOR
#include "VReceiveGainCalibration.h"
#include <verilated.h>
using Device = VReceiveGainCalibration;
#else
#include "../receive_calibration.hpp"
long _system_clock = -1;
using Device = ReceiveGainCalibration;
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
        Device device;
        bool enable = true, start = false, valid = false, finish = false;
        unsigned input = 0, level = 0, cycles = 0;
#ifndef VERILATOR
        device.enable_in = _ASSIGN(enable);
        device.start_in = _ASSIGN(start);
        device.valid_in = _ASSIGN(valid);
        device.finish_in = _ASSIGN(finish);
        device.sample_in = _ASSIGN(input);
        device.level_in = _ASSIGN(level);
        device._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        std::array<unsigned, 6> output{};
        auto edge = [&](bool reset = false) {
#ifdef VERILATOR
            device.clk = 0;
            device.reset = reset;
            device.enable_in = enable;
            device.start_in = start;
            device.valid_in = valid;
            device.finish_in = finish;
            device.sample_in = input;
            device.level_in = level;
            device.eval();
            device.clk = 1;
            device.eval();
            output = {unsigned(device.offset_out), unsigned(device.gain_out), unsigned(device.ready_out),
                      unsigned(device.fault_out), unsigned(device.busy_out), unsigned(device.collecting_out)};
#else
            ++_system_clock;
            device._work(reset);
            device._strobe();
            output = {unsigned(device.offset_out()), unsigned(device.gain_out()), unsigned(device.ready_out()),
                      unsigned(device.fault_out()), unsigned(device.busy_out()), unsigned(device.collecting_out())};
#endif
            if (trace) {
                trace << cycles;
                for (unsigned value : output)
                    trace << ',' << value;
                trace << '\n';
            }
            ++cycles;
        };
        auto require = [&](bool condition, const char* message) {
            if (!condition)
                throw std::runtime_error(std::string(message) + " at clock " + std::to_string(cycles));
        };
        auto begin = [&] {
            valid = false;
            finish = false;
            start = true;
            edge();
            start = false;
            require(output[2] == 0 && output[3] == 0 && output[4] == 1 && output[5] == 1,
                    "training start/restart");
        };
        auto train = [&](const std::array<unsigned, 5>& codes, unsigned population, bool tiny_span = false) {
            begin();
            std::array<unsigned long long, 5> sums{};
            for (unsigned repeat = 0; repeat < population; ++repeat) {
                // Permute labels independently of their expected voltage order.
                for (unsigned position = 0; position < 5; ++position) {
                    level = (3 * position + repeat) % 5;
                    input = tiny_span ? 128 + (repeat + 1 == population ? level : 0) : codes[level];
                    sums[level] += input;
                    valid = true;
                    edge();
                    require(output[2] == 0 && output[3] == 0 && output[5] == 1, "training collection");
                    if (repeat % 101 == 100) {
                        valid = false;
                        input = 255;
                        level = 7;
                        edge();
                        require(output[3] == 0, "invalid bubbles must be ignored");
                    }
                }
            }
            valid = false;
            finish = true;
            edge();
            finish = false;
            require(output[2] == 0 && output[3] == 0 && output[4] == 1 && output[5] == 0,
                    "division must begin after balanced training");
            for (unsigned delay = 1; delay <= 64; ++delay) {
                edge();
                if (delay < 64)
                    require(output[2] == 0 && output[3] == 0 && output[4] == 1, "early calibration publication");
            }
            const long expected_offset = std::lround(static_cast<long double>(sums[2]) / population);
            const long expected_gain = std::lround(32768.L * population / (sums[4] - sums[0]));
            if (expected_gain > 65535) {
                require(output[2] == 0 && output[3] == 1 && output[4] == 0, "unrepresentable gain");
            } else {
                require(output[0] == unsigned(expected_offset) && output[1] == unsigned(expected_gain) &&
                        output[2] == 1 && output[3] == 0 && output[4] == 0, "calibration mean/span oracle");
                const auto retained = output;
                for (unsigned delay = 0; delay < 4; ++delay)
                    edge();
                require(output == retained, "published calibration retention");
            }
        };
        edge(true);
        require(output[0] == 128 && output[1] == 256 && output[2] == 0 && output[4] == 0, "reset defaults");
        for (unsigned center : {64u, 128u, 191u})
            for (unsigned spacing : {4u, 16u, 30u})
                for (unsigned population : {1u, 3u, 17u, 257u})
                    train({center - 2 * spacing, center - spacing, center, center + spacing, center + 2 * spacing},
                          population);
        train({97, 107, 118, 128, 137}, 17); // Measured first receive-front-end transfer.
        require(output[0] == 118 && output[1] == 819, "receive-front-end gain and offset");
        train({0, 64, 128, 192, 255}, 65535); // Full accumulator/count range.
        train({0, 0, 0, 0, 0}, 257, true); // Monotonic means but unrepresentable gain.
        for (unsigned invalid_level : {5u, 6u, 7u}) {
            begin();
            level = invalid_level;
            valid = true;
            edge();
            valid = false;
            require(output[3] == 1 && output[2] == 0 && output[4] == 0, "invalid training label");
        }
        for (bool simultaneous : {false, true}) {
            begin();
            input = 128;
            level = 2;
            valid = true;
            edge();
            valid = simultaneous;
            finish = true;
            edge();
            valid = finish = false;
            require(output[3] == 1 && output[2] == 0 && output[4] == 0, "incomplete or simultaneous finish");
        }
        begin();
        input = 128;
        valid = true;
        for (level = 0; level < 5; ++level)
            edge();
        valid = false;
        finish = true;
        edge();
        finish = false;
        require(output[3] == 1 && output[2] == 0, "flat training has no observable gain");
        begin();
        valid = true;
        level = 0;
        input = 128;
        for (unsigned sample = 0; sample < 65535; ++sample)
            edge();
        require(output[3] == 0 && output[5] == 1, "maximum bin population");
        edge();
        valid = false;
        require(output[3] == 1 && output[2] == 0 && output[4] == 0, "bin population overflow");
        for (unsigned interrupted_after : {17u, 47u, 63u}) {
            begin();
            valid = true;
            for (level = 0; level < 5; ++level) {
                input = 40 + 40 * level;
                edge();
            }
            valid = false;
            finish = true;
            edge();
            finish = false;
            for (unsigned delay = 0; delay < interrupted_after; ++delay)
                edge();
            edge(true);
            for (unsigned delay = 0; delay < 65; ++delay)
                edge();
            require(output[0] == 128 && output[1] == 256 && output[2] == 0 && output[3] == 0 && output[4] == 0,
                    "reset must discard both partial division results");
        }
        begin();
        enable = false;
        edge();
        require(output[2] == 0 && output[3] == 0 && output[4] == 0 && output[0] == 128 && output[1] == 256,
                "disable flush");
        enable = true;
        train({20, 60, 100, 140, 180}, 3);
        edge(true);
        require(output[2] == 0 && output[4] == 0, "reset flush");
        std::cout << "PASS " << cycles << " calibration clocks; independent means/span, population limits, "
                  << "rounding, gaps, label faults, missing/flat training, overflow and restart\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
