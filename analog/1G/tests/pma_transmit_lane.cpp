#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VPMATransmitLane.h"
using DUT = VPMATransmitLane;
#else
#include "../pma_transmit_lane.hpp"
using DUT = PMATransmitLane;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("expected lane vectors and trace");
        std::ifstream input(argv[1]);
        std::ofstream trace(argv[2]);
        if (!input || !trace)
            throw std::runtime_error("cannot open lane vectors or trace");
        DUT dut;
        std::array<unsigned, 6> inputs{};
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(inputs[1] != 0);
        dut.symbol_in = _ASSIGN(inputs[2]);
        dut.test_mode_in = _ASSIGN(inputs[3]);
        dut.test_seed_in = _ASSIGN(inputs[4]);
        dut.restart_in = _ASSIGN(inputs[5] != 0);
        dut._assign();
#endif
        unsigned row = 0;
        while (input >> inputs[0]) {
            for (unsigned i = 1; i < inputs.size(); ++i)
                if (!(input >> inputs[i]))
                    throw std::runtime_error("truncated lane input");
            std::array<unsigned, 7> expected{}, actual{};
            for (auto& value : expected)
                if (!(input >> value))
                    throw std::runtime_error("truncated lane expectation");
#ifdef VERILATOR
            dut.reset = inputs[0];
            dut.enable_in = inputs[1];
            dut.symbol_in = inputs[2];
            dut.test_mode_in = inputs[3];
            dut.test_seed_in = inputs[4];
            dut.restart_in = inputs[5];
            dut.clk = 0;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            actual = {unsigned(dut.main_p_out), unsigned(dut.main_n_out),
                      unsigned(dut.history_p_out), unsigned(dut.history_n_out),
                      unsigned(dut.valid_out), unsigned(dut.fault_out), unsigned(dut.slave_clock_out)};
#else
            ++_system_clock;
            dut._work(inputs[0]);
            dut._strobe();
            actual = {unsigned(dut.main_p_out()), unsigned(dut.main_n_out()),
                      unsigned(dut.history_p_out()), unsigned(dut.history_n_out()),
                      unsigned(dut.valid_out()), unsigned(dut.fault_out()), unsigned(dut.slave_clock_out())};
#endif
            if (actual != expected) {
                std::cerr << "actual:";
                for (auto value : actual)
                    std::cerr << ' ' << value;
                std::cerr << " expected:";
                for (auto value : expected)
                    std::cerr << ' ' << value;
                std::cerr << '\n';
                throw std::runtime_error("transmit lane mismatch at row " + std::to_string(row));
            }
            for (unsigned i = 0; i < actual.size(); ++i)
                trace << (i ? "," : "") << actual[i];
            trace << '\n';
            ++row;
        }
        if (row < 10000)
            throw std::runtime_error("insufficient lane coverage");
        std::cout << row << " independently checked transmit-lane clocks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
