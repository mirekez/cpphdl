#ifdef VERILATOR
#include "VFLPTransmitter.h"
#include <verilated.h>
#else
#include "../flp_transmit.hpp"
long _system_clock = -1;
#endif
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
#ifdef VERILATOR
        VFLPTransmitter dut;
#else
        FLPTransmitter dut;
#endif
        unsigned word = 0;
        bool enable = false, restart = false;
#ifndef VERILATOR
        dut.page_in = _ASSIGN(word);
        dut.enable_in = _ASSIGN(enable);
        dut.restart_in = _ASSIGN(restart);
        dut._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        unsigned observations = 0;
        bool previous_pulse = false, previous_begin = false;
        auto tick = [&](bool reset, bool expected_pulse, bool expected_begin, bool expected_done = false) {
            bool pulse, begin, done;
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.page_in = word;
            dut.enable_in = enable;
            dut.restart_in = restart;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            pulse = dut.pulse_out;
            begin = dut.burst_begin_out;
            done = dut.burst_done_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            pulse = dut.pulse_out();
            begin = dut.burst_begin_out();
            done = dut.burst_done_out();
#endif
            if (pulse != expected_pulse || begin != expected_begin || done != expected_done)
                throw std::runtime_error("FLP wire-time mismatch at clock " + std::to_string(observations));
            if (trace && (observations == 0 || pulse != previous_pulse || begin != previous_begin || done || expected_done))
                trace << observations << ',' << pulse << ',' << begin << ',' << done << '\n';
            previous_pulse = pulse;
            previous_begin = begin;
            ++observations;
        };
        tick(true, false, false);
        const std::array<unsigned, 6> pages{0, 65535, 0xA5A5, 0x5A5A, 1, 0x8000};
        enable = true;
        // The oracle constructs wire pulse intervals directly from bit times,
        // independently of the DUT's half-slot and pulse-width counters.
        for (unsigned page : pages) {
            std::vector<unsigned> pulses;
            for (unsigned bit = 0; bit <= 16; ++bit) {
                pulses.push_back(bit * 15625);
                if (bit != 16 && ((page >> bit) & 1))
                    pulses.push_back(bit * 15625 + 7812);
            }
            unsigned index = 0;
            for (unsigned cycle = 0; cycle < 2000000; ++cycle) {
                word = cycle < 64 ? page : page ^ 65535; // Must not change the captured page.
                while (index < pulses.size() && cycle >= pulses[index] + 13)
                    ++index;
                const bool high = index < pulses.size() && cycle >= pulses[index] && cycle < pulses[index] + 13;
                tick(false, high, cycle == 0, cycle == 250013);
            }
        }
        // Abort an active pulse, remain quiet, and require a fresh captured page.
        word = 65535;
        tick(false, true, true);
        enable = false;
        for (unsigned cycle = 0; cycle < 1000; ++cycle)
            tick(false, false, false);
        enable = true;
        tick(false, true, true);
        restart = true;
        tick(false, false, false);
        restart = false;
        word = 0;
        tick(false, true, true);
        tick(true, false, false);
        tick(false, true, true);
        std::cout << observations << " independently checked FLP clocks; six complete bursts, page holds and restart/reset passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
