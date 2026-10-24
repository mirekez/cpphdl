#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

#ifdef VERILATOR
#include "VReceiveDSP.h"
using DUT = VReceiveDSP;
#else
#include "../receive_dsp.hpp"
using DUT = ReceiveDSP;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("usage: receive_dsp vectors trace");
        std::ifstream input(argv[1]);
        std::ofstream trace(argv[2]);
        if (!input || !trace)
            throw std::runtime_error("cannot open fixture or trace");
        DUT dut;
        std::array<std::uint32_t, 9> v{};
        std::array<std::uint32_t, 6> actual{}, expected{};
        unsigned row = 0, valid = 0;
#ifndef VERILATOR
        dut.samples_in = _ASSIGN(v[0]);
        dut.local_in = _ASSIGN(v[1]);
        dut.desired_in = _ASSIGN(v[2]);
        dut.tag_in = _ASSIGN(v[3]);
        dut.clipped_in = _ASSIGN(v[4] != 0);
        dut.train_in = _ASSIGN(v[5] != 0);
        dut.valid_in = _ASSIGN(v[6] != 0);
        dut.restart_in = _ASSIGN(v[7] != 0);
        dut._assign();
#endif
        while (input >> v[0]) {
            for (unsigned i = 1; i < v.size(); ++i)
                if (!(input >> v[i]))
                    throw std::runtime_error("truncated inputs");
            for (auto& value : expected)
                if (!(input >> value))
                    throw std::runtime_error("truncated expectations");
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = v[8];
            dut.samples_in = v[0];
            dut.local_in = v[1];
            dut.desired_in = v[2];
            dut.tag_in = v[3];
            dut.clipped_in = v[4];
            dut.train_in = v[5];
            dut.valid_in = v[6];
            dut.restart_in = v[7];
            dut.eval();
            actual = {dut.samples_out, dut.tag_out, dut.valid_out, dut.fault_out,
                      dut.clipped_out, dut.training_busy_out};
#else
            ++_system_clock;
            // Observe settled pre-edge state; all child ports use this epoch.
            actual = {unsigned(dut.samples_out()), unsigned(dut.tag_out()),
                      unsigned(dut.valid_out()), unsigned(dut.fault_out()),
                      unsigned(dut.clipped_out()), unsigned(dut.training_busy_out())};
#endif
            if (row != 0) {
                for (unsigned i = 0; i < actual.size(); ++i) {
                    bool payload = i == 0 || i == 1 || i == 4;
                    if ((!payload || expected[2]) && actual[i] != expected[i])
                        throw std::runtime_error("row " + std::to_string(row) + " field " + std::to_string(i) +
                                                 " got " + std::to_string(actual[i]) + " expected " + std::to_string(expected[i]));
                    trace << (i ? "," : "") << (payload && !actual[2] ? 0 : actual[i]);
                }
                trace << '\n';
                valid += actual[2];
            }
#ifdef VERILATOR
            dut.clk = 1;
            dut.eval();
#else
            dut._work(v[8] != 0);
            dut._strobe();
#endif
            ++row;
        }
        if (row < 39000 || valid < 37000)
            throw std::runtime_error("insufficient DSP integration coverage");
        std::cout << "PASS " << row << " clocks, " << valid << " four-lane observations\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
