#ifdef VERILATOR
#include "VFLPReceiver.h"
#include <verilated.h>
#else
#include "../flp_receive.hpp"
long _system_clock = -1;
#endif
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
#ifdef VERILATOR
        VFLPReceiver dut;
#else
        FLPReceiver dut;
#endif
        bool pulse = false, enable = true;
#ifndef VERILATOR
        dut.pulse_in = _ASSIGN(pulse);
        dut.enable_in = _ASSIGN(enable);
        dut._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        unsigned observed = 0, received = 0, faults = 0, last_word = 0, previous = ~0u;
        auto tick = [&](bool reset, bool expected_valid, unsigned expected_word) {
            unsigned word;
            bool valid, fault;
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.pulse_in = pulse;
            dut.enable_in = enable;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            word = dut.page_out;
            valid = dut.valid_out;
            fault = dut.fault_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            word = dut.page_out();
            valid = dut.valid_out();
            fault = dut.fault_out();
#endif
            if (valid != expected_valid || word != expected_word)
                throw std::runtime_error("FLP receive mismatch at clock " + std::to_string(observed));
            received += valid;
            faults += fault;
            last_word = word;
            const unsigned packed = word | (unsigned(valid) << 16) | (unsigned(fault) << 17);
            if (trace && packed != previous)
                trace << observed << ',' << word << ',' << valid << ',' << fault << '\n';
            previous = packed;
            ++observed;
        };
        tick(true, false, 0);
        // Independently construct wire intervals, including timing jitter,
        // widths, missing clocks, duplicate data and runt/overlong pulses.
        for (unsigned scenario = 0; scenario < 12; ++scenario) {
            std::vector<std::pair<unsigned, unsigned>> intervals;
            const unsigned page = std::array<unsigned, 6>{0, 65535, 0xA5A5, 0x5A5A, 1, 0x8000}[scenario % 6];
            const unsigned width = scenario % 3 == 0 ? 6 : scenario % 3 == 1 ? 13 : 24;
            unsigned ending = 0, duplicate = 0;
            for (unsigned bit = 0; bit <= 16; ++bit) {
                const int jitter = scenario & 1 ? int((bit * 379) % 1401) - 700 : 0;
                const unsigned clock = unsigned(2000 + int(bit * 15625) + jitter);
                if (!(scenario == 8 && bit == 7))
                    intervals.emplace_back(clock, clock + width);
                if (bit == 16)
                    ending = clock + width + 2;
                if (bit < 16 && ((page >> bit) & 1)) {
                    intervals.emplace_back(clock + 7812, clock + 7812 + width);
                    if (!duplicate)
                        duplicate = clock + 9000;
                }
            }
            if (scenario == 6 || scenario == 7)
                intervals.emplace_back(2000 + 3000, 2000 + 3003); // Ignored runt.
            if (scenario == 9)
                intervals.emplace_back(duplicate, duplicate + 13); // A second data pulse.
            if (scenario == 10)
                intervals[5].second = intervals[5].first + 40;
            if (scenario == 11)
                intervals.resize(10); // Truncated burst must time out.
            std::sort(intervals.begin(), intervals.end());
            unsigned index = 0;
            const bool accepts = scenario < 8;
            const unsigned before = faults;
            const unsigned old_word = last_word;
            for (unsigned cycle = 0; cycle < 280000; ++cycle) {
                while (index < intervals.size() && cycle >= intervals[index].second)
                    ++index;
                pulse = index < intervals.size() && cycle >= intervals[index].first;
                const bool valid = accepts && cycle == ending;
                tick(false, valid, accepts && cycle >= ending ? page : old_word);
            }
            if (accepts && faults != before)
                throw std::runtime_error("valid/jittered FLP burst faulted");
            if (!accepts && faults == before)
                throw std::runtime_error("malformed FLP burst escaped fault detection");
        }
        enable = false;
        pulse = true;
        tick(false, false, 0);
        tick(true, false, 0);
        if (received != 8)
            throw std::runtime_error("wrong independent FLP page coverage");
        std::cout << observed << " independently checked FLP receive clocks; eight pages and four rejected bursts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
