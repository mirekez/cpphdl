#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VReceiveFilter.h"
using DUT = VReceiveFilter;
#else
#include "../receive_filter_datapath.hpp"
using DUT = ReceiveFilter;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("usage: filter_datapath fixture [trace]");
        std::ifstream input(argv[1]);
        if (!input)
            throw std::runtime_error("missing independently generated filter fixture");
        std::ofstream trace;
        if (argc == 3)
            trace.open(argv[2]);
        DUT dut;
        std::array<unsigned, 6> values{};
#ifndef VERILATOR
        dut.sample_in = _ASSIGN(values[0]);
        dut.local_in = _ASSIGN(values[1]);
        dut.desired_in = _ASSIGN(values[2]);
        dut.train_in = _ASSIGN(values[3] != 0);
        dut.advance_in = _ASSIGN(values[4] != 0);
        dut._assign();
#endif
        unsigned row = 0;
        while (input >> values[0]) {
            for (unsigned i = 1; i < values.size(); ++i)
                if (!(input >> values[i]))
                    throw std::runtime_error("truncated filter inputs");
            // Share the independent full-state vectors. The underlying filter
            // test checks all 24 coefficients; this top checks its three ports.
            std::array<unsigned, 27> expected{};
            for (auto& value : expected)
                if (!(input >> value))
                    throw std::runtime_error("truncated filter expectations");
            std::array<unsigned, 3> actual{};
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = values[5];
            dut.sample_in = values[0];
            dut.local_in = values[1];
            dut.desired_in = values[2];
            dut.train_in = values[3];
            dut.advance_in = values[4];
            dut.eval();
            dut.clk = 1;
            dut.eval();
            actual = {unsigned(dut.sample_out), unsigned(dut.valid_out), unsigned(dut.fault_out)};
#else
            ++_system_clock;
            dut._work(values[5]);
            dut._strobe();
            actual = {unsigned(dut.sample_out()), unsigned(dut.valid_out()), unsigned(dut.fault_out())};
#endif
            for (unsigned i = 0; i < actual.size(); ++i) {
                if (actual[i] != expected[i])
                    throw std::runtime_error("receive datapath contract mismatch at row " + std::to_string(row));
                if (trace)
                    trace << (i ? "," : "") << actual[i];
            }
            if (trace)
                trace << '\n';
            ++row;
        }
        if (row < 37000)
            throw std::runtime_error("insufficient datapath coverage");
        std::cout << row << " independent receive-datapath observations passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
