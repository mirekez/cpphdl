#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VPMATestPattern.h"
using DUT = VPMATestPattern;
#else
#include "../pma_test_pattern.hpp"
using DUT = PMATestPattern;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("expected fixture and output trace");
        std::ifstream input(argv[1]);
        std::ofstream trace(argv[2]);
        if (!input || !trace)
            throw std::runtime_error("cannot open pattern fixture or trace");
        DUT dut;
        std::array<unsigned, 5> values{};
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(values[1] != 0);
        dut.mode_in = _ASSIGN(values[2]);
        dut.seed_in = _ASSIGN(values[3]);
        dut.restart_in = _ASSIGN(values[4] != 0);
        dut._assign();
#endif
        unsigned row = 0;
        while (input >> values[0]) {
            for (unsigned i = 1; i < values.size(); ++i)
                if (!(input >> values[i]))
                    throw std::runtime_error("truncated test-pattern input");
            std::array<unsigned, 4> expected{}, actual{};
            for (auto& value : expected)
                if (!(input >> value))
                    throw std::runtime_error("truncated test-pattern expectation");
#ifdef VERILATOR
            dut.reset = values[0];
            dut.enable_in = values[1];
            dut.mode_in = values[2];
            dut.seed_in = values[3];
            dut.restart_in = values[4];
            dut.clk = 0;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            actual = {unsigned(dut.symbol_out), unsigned(dut.valid_out),
                      unsigned(dut.slave_clock_out), unsigned(dut.fault_out)};
#else
            ++_system_clock;
            dut._work(values[0]);
            dut._strobe();
            actual = {unsigned(dut.symbol_out()), unsigned(dut.valid_out()),
                      unsigned(dut.slave_clock_out()), unsigned(dut.fault_out())};
#endif
            if (actual != expected)
                throw std::runtime_error("test-pattern mismatch at row " + std::to_string(row));
            for (unsigned i = 0; i < actual.size(); ++i)
                trace << (i ? "," : "") << actual[i];
            trace << '\n';
            ++row;
        }
        if (row < 10000)
            throw std::runtime_error("insufficient test-pattern coverage");
        std::cout << row << " independently checked test-pattern clocks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
