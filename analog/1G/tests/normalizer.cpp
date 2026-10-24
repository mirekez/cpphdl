#ifdef VERILATOR
#include "VReceiveSampleNormalizer.h"
#include <verilated.h>
using Device = VReceiveSampleNormalizer;
#else
#include "../receive_normalizer.hpp"
long _system_clock = -1;
using Device = ReceiveSampleNormalizer;
#endif
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        Device device;
        unsigned input = 0, offset = 128, gain = 424;
        bool valid = true, enable = true;
#ifndef VERILATOR
        device.sample_in = _ASSIGN(input);
        device.offset_in = _ASSIGN(offset);
        device.gain_in = _ASSIGN(gain);
        device.valid_in = _ASSIGN(valid);
        device.enable_in = _ASSIGN(enable);
        device._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        unsigned cycles = 0, previous = 128, clipped_cases = 0;
        struct Pending { long desired; bool valid; };
        std::deque<Pending> pending(7, {128, false});
        auto edge = [&](bool reset = false) {
            const long desired = 128 + std::lround((double(input) - offset) * double(gain) / 256);
            const Pending ready = pending.front();
            pending.pop_front();
            pending.push_back({desired, valid});
            if (reset || !enable)
                pending.assign(7, {128, false});
            const bool expected_valid = !reset && enable && ready.valid;
            const bool expected_clip = expected_valid && (ready.desired < 0 || ready.desired > 255);
            const unsigned expected = reset || !enable ? 128 : expected_valid ? std::clamp(ready.desired, 0L, 255L) : previous;
            unsigned actual;
            bool actual_valid, actual_clip;
#ifdef VERILATOR
            device.clk = 0;
            device.reset = reset;
            device.sample_in = input;
            device.offset_in = offset;
            device.gain_in = gain;
            device.valid_in = valid;
            device.enable_in = enable;
            device.eval();
            device.clk = 1;
            device.eval();
            actual = unsigned(device.sample_out);
            actual_valid = device.valid_out;
            actual_clip = device.clipped_out;
#else
            ++_system_clock;
            device._work(reset);
            device._strobe();
            actual = unsigned(device.sample_out());
            actual_valid = device.valid_out();
            actual_clip = device.clipped_out();
#endif
            if (trace)
                trace << cycles << ',' << actual << ',' << actual_valid << ',' << actual_clip << '\n';
            if (actual != expected || actual_valid != expected_valid || actual_clip != expected_clip)
                throw std::runtime_error("ADC normalization contract at clock " + std::to_string(cycles));
            previous = expected;
            clipped_cases += expected_clip;
            ++cycles;
        };
        edge(true);
        for (unsigned calibration_offset : {0u, 64u, 128u, 192u, 255u}) {
            offset = calibration_offset;
            for (unsigned calibration_gain : {0u, 1u, 128u, 256u, 257u, 258u, 424u, 511u, 513u, 1024u, 32768u, 65535u}) {
                gain = calibration_gain;
                for (input = 0; input < 256; ++input) {
                    valid = true;
                    edge();
                    if (input % 19 == 0) {
                        valid = false;
                        edge();
                    }
                }
            }
        }
        // Every calibration word at six signed input differences exercises
        // digit carries and sign extension against the independent arithmetic
        // oracle, including the largest positive and negative ADC difference.
        for (int difference : {-255, -128, -1, 1, 127, 255}) {
            input = difference < 0 ? 0 : unsigned(difference);
            offset = difference < 0 ? unsigned(-difference) : 0;
            valid = true;
            for (gain = 0; gain < 65536; ++gain)
                edge();
        }
        // Nominal 3.3 V ADC codes for {-2,-1,0,+1,+2} at 0.25 V per level.
        // The PCS sees approximately {64,96,128,160,192}; quantization remains.
        offset = 128;
        gain = 424;
        valid = true;
        for (unsigned code : {89u, 109u, 128u, 147u, 167u}) {
            input = code;
            for (unsigned stage = 0; stage < 8; ++stage)
                edge();
            const int symbol = int(std::lround((int(code) - 128) / (255. * .25 / 3.3)));
            if (std::abs(int(previous) - (128 + 32 * symbol)) > 2)
                throw std::runtime_error("physical ADC PAM spacing does not match the normalized PCS convention");
        }
        enable = false;
        edge();
        enable = true;
        edge();
        edge(true);
        for (unsigned stage = 0; stage < 8; ++stage) {
            input = 255 - stage;
            edge();
        }
        edge(true); // Flush accepted samples while they are still in flight.
        valid = false;
        for (unsigned stage = 0; stage < 8; ++stage)
            edge();
        if (clipped_cases < 1000)
            throw std::runtime_error("normalizer saturation was insufficiently exercised");
        std::cout << "PASS " << cycles << " normalization clocks; all ADC codes, calibration extrema, "
                  << "rounding, clipping, valid gaps, reset and disable\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
