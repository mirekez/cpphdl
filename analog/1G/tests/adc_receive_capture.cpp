#include <array>
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VADCReceiveCapture.h"
using DUT = VADCReceiveCapture;
#define READ(name) unsigned(dut.name)
#else
#include "../adc_receive_capture.hpp"
using DUT = ADCReceiveCapture;
long _system_clock = -1;
#define READ(name) unsigned(dut.name())
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected trace destination");
        std::ofstream trace(argv[1]);
        DUT dut;
        bool enable = false, sampling = false, train = false;
        unsigned time = 0, local = 0, desired = 0;
        std::array<unsigned, 4> comparisons{}, offset{128, 112, 140, 128}, gain{256, 424, 192, 512};
        std::array<unsigned, 64> held{};
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(enable);
        dut.sample_enable_in = _ASSIGN(sampling);
        dut.time_in = _ASSIGN(time);
        dut.local_in = _ASSIGN(local);
        dut.desired_in = _ASSIGN(desired);
        dut.train_in = _ASSIGN(train);
        for (unsigned i = 0; i < 4; ++i) {
            dut.comparisons_in[i] = _ASSIGN_I(comparisons[i]);
            dut.offset_in[i] = _ASSIGN_I(offset[i]);
            dut.gain_in[i] = _ASSIGN_I(gain[i]);
        }
        dut._assign();
#endif
        auto settle = [&](bool reset) {
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.enable_in = enable;
            dut.sample_enable_in = sampling;
            dut.time_in = time;
            dut.local_in = local;
            dut.desired_in = desired;
            dut.train_in = train;
            for (unsigned i = 0; i < 4; ++i) {
                dut.comparisons_in[i] = comparisons[i];
                dut.offset_in[i] = offset[i];
                dut.gain_in[i] = gain[i];
            }
            dut.eval();
#else
            ++_system_clock;
#endif
        };
        auto edge = [&](bool reset) {
#ifdef VERILATOR
            dut.clk = 1;
            dut.eval();
#else
            dut._work(reset);
            dut._strobe();
#endif
        };
        struct Expected { unsigned clock, word, time, local, desired; bool train, clipped; };
        std::deque<Expected> expected;
        unsigned checked = 0, apertures = 0;
        for (unsigned profile = 0; profile < 3; ++profile) {
            enable = false;
            sampling = false;
            for (unsigned i = 0; i < 4; ++i) { settle(true); edge(true); }
            expected.clear();
            held.fill(0);
            enable = true;
            for (unsigned clock = 0; clock < 76000; ++clock) {
                time = (65520 + clock) & 65535;
                local = (clock * 53 + profile) & 4095;
                desired = (clock * 117 + 9) & 4095;
                train = (clock % 19) < 7;
                sampling = clock < 75960 && (profile != 1 || clock % 11 != 3);
                settle(false);
                const unsigned aperture = READ(aperture_out);
                if (aperture) {
                    Expected e{clock + 19, 0, time, local, desired, train, false};
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        const unsigned code = ((clock * 73) ^ (clock >> 3) ^ (lane * 113)) & 255;
                        for (unsigned slice = 0; slice < 13; ++slice)
                            if ((aperture >> slice) & 1)
                                held[lane * 16 + slice] = code;
                        const int product = (int(code) - int(offset[lane])) * int(gain[lane]);
                        const int rounded = product < 0 ? -((-product + 128) / 256) : (product + 128) / 256;
                        const int value = 128 + rounded;
                        e.word |= unsigned(value < 0 ? 0 : value > 255 ? 255 : value) << (lane * 8);
                        e.clipped |= value < 0 || value > 255;
                    }
                    expected.push_back(e);
                    ++apertures;
                }
                comparisons.fill(0);
                for (unsigned i = 0; i < 64; ++i)
                    if (held[i] >= READ(trial_out[i]))
                        comparisons[i / 16] |= 1u << (i % 16);
                settle(false);
                const bool valid = !expected.empty() && expected.front().clock == clock;
                if (READ(fault_out) || bool(READ(valid_out)) != valid)
                    throw std::runtime_error("capture cadence/fault at clock " + std::to_string(clock));
                if (valid) {
                    const auto e = expected.front();
                    expected.pop_front();
                    if (READ(samples_out) != e.word || READ(tag_out) != e.time || READ(local_out) != e.local ||
                        READ(desired_out) != e.desired || bool(READ(train_out)) != e.train ||
                        bool(READ(clipped_out)) != e.clipped)
                        throw std::runtime_error("aperture metadata or normalized code mismatch at " + std::to_string(clock));
                    ++checked;
                }
                trace << profile << ',' << clock << ',' << aperture << ',' << READ(valid_out) << ','
                      << (valid ? READ(samples_out) : 0) << ',' << (valid ? READ(tag_out) : 0) << ','
                      << (valid ? READ(local_out) : 0) << ',' << (valid ? READ(desired_out) : 0) << ','
                      << (valid ? READ(train_out) : 0) << ',' << (valid ? READ(clipped_out) : 0) << '\n';
                edge(false);
            }
            if (!expected.empty())
                throw std::runtime_error("undelivered ADC samples");
            // Disable in-flight conversions as well as fully drained runs.
            sampling = true;
            for (unsigned i = 0; i < 10; ++i) { settle(false); edge(false); }
            enable = false;
            settle(false);
            if (READ(valid_out) || READ(startup_ready_out) || READ(aperture_out))
                throw std::runtime_error("capture remained available on supply/clock withdrawal");
            edge(false);
        }
        if (checked != apertures || checked < 200000 || !trace)
            throw std::runtime_error("missing aperture wrap, clipping, gap or restart coverage");
        std::cout << "PASS capture " << checked << " aperture-aligned quartets\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
