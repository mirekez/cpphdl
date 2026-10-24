#ifdef VERILATOR
#include "VPAM5DriverControl.h"
#include <verilated.h>
using Device = VPAM5DriverControl;
#else
#include "../pam5_driver_control.hpp"
long _system_clock = -1;
using Device = PAM5DriverControl;
#endif
#include <array>
#include <bitset>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        Device device;
        unsigned code = 2, previous = 2, cycles = 0;
        bool enable = true, fault = false;
#ifndef VERILATOR
        device.symbol_in = _ASSIGN(code);
        device.enable_in = _ASSIGN(enable);
        device._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        std::bitset<17> levels;
        auto edge = [&](bool reset = false) {
            const bool valid = !reset && enable && !fault && code <= 4;
            std::array<unsigned, 4> expected{};
            if (valid) {
                // Build each unary gate independently of the DUT's shifts.
                for (unsigned cell = 0; cell < 4; ++cell) {
                    expected[0] |= unsigned(cell < code) << cell;
                    expected[1] |= unsigned(cell >= code) << cell;
                    expected[2] |= unsigned(cell < previous) << cell;
                    expected[3] |= unsigned(cell >= previous) << cell;
                }
                const int weighted = 3 * (2 - int(code)) + (2 - int(previous));
                levels.set(weighted + 8);
                previous = code;
            } else {
                previous = 2;
            }
            if (!reset && enable && code > 4)
                fault = true;
            if (reset)
                fault = false;
            std::array<unsigned, 4> actual;
            bool actual_valid, actual_fault;
#ifdef VERILATOR
            device.clk = 0;
            device.reset = reset;
            device.symbol_in = code;
            device.enable_in = enable;
            device.eval();
            device.clk = 1;
            device.eval();
            actual = {unsigned(device.main_p_out), unsigned(device.main_n_out),
                      unsigned(device.history_p_out), unsigned(device.history_n_out)};
            actual_valid = device.valid_out;
            actual_fault = device.fault_out;
#else
            ++_system_clock;
            device._work(reset);
            device._strobe();
            actual = {unsigned(device.main_p_out()), unsigned(device.main_n_out()),
                      unsigned(device.history_p_out()), unsigned(device.history_n_out())};
            actual_valid = device.valid_out();
            actual_fault = device.fault_out();
#endif
            if (actual != expected || actual_valid != valid || actual_fault != fault)
                throw std::runtime_error("PAM5 driver control/history/fault mismatch at clock " + std::to_string(cycles));
            if (trace)
                trace << cycles << ',' << actual[0] << ',' << actual[1] << ',' << actual[2] << ',' << actual[3]
                      << ',' << actual_valid << ',' << actual_fault << '\n';
            ++cycles;
        };
        edge(true);
        for (unsigned a = 0; a < 5; ++a)
            for (unsigned b = 0; b < 5; ++b)
                for (unsigned c = 0; c < 5; ++c) {
                    code = a; edge();
                    code = b; edge();
                    code = c; edge();
                    enable = false; edge();
                    enable = true; edge();
                }
        for (code = 5; code <= 7; ++code) {
            edge();
            enable = false; edge();
            enable = true; edge();
            edge(true);
        }
        if (!levels.all())
            throw std::runtime_error("not all seventeen weighted output levels were exercised");
        std::cout << "PASS " << cycles << " PAM5 control clocks: all symbol triples, seventeen weighted levels, "
                     "neutral startup, disable, invalid codes and sticky fault reset\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
