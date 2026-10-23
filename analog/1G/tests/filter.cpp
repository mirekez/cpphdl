#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#ifdef PIPELINED_FILTER
#include "VPipelinedReceiveFilter.h"
using DUT = VPipelinedReceiveFilter;
#else
#include "VReceiveAdaptiveFilter.h"
using DUT = VReceiveAdaptiveFilter;
#endif
#else
#ifdef PIPELINED_FILTER
#include "../receive_filter_pipeline.hpp"
using DUT = PipelinedReceiveFilter;
#else
#include "../receive_filter.hpp"
using DUT = ReceiveAdaptiveFilter;
#endif
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("usage: filter fixture [trace]");
        std::ifstream input(argv[1]);
        if (!input)
            throw std::runtime_error("missing fixed-point filter fixture");
        std::ofstream trace;
        if (argc == 3)
            trace.open(argv[2]);
        DUT dut;
        std::array<unsigned, 6> values{};
#ifndef VERILATOR
        dut.sample_in = [&] { return u<8>(values[0]); };
        dut.local_in = [&] { return u<12>(values[1]); };
        dut.desired_in = [&] { return u<3>(values[2]); };
        dut.train_in = [&] { return values[3] != 0; };
        dut.advance_in = [&] { return values[4] != 0; };
        dut._assign();
#endif
        unsigned row = 0;
        while (input >> values[0]) {
            for (unsigned i = 1; i < values.size(); ++i)
                if (!(input >> values[i]))
                    throw std::runtime_error("truncated filter inputs");
            std::array<unsigned, 27> expected{}, actual{};
            for (auto& value : expected)
                if (!(input >> value))
                    throw std::runtime_error("truncated filter expectations");
#ifdef VERILATOR
            dut.sample_in = values[0];
            dut.local_in = values[1];
            dut.desired_in = values[2];
            dut.train_in = values[3];
            dut.advance_in = values[4];
            dut.reset = values[5];
            dut.clk = 0;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            actual[0] = dut.sample_out;
            actual[1] = dut.valid_out;
            actual[2] = dut.fault_out;
            for (unsigned i = 0; i < 24; ++i) {
#ifdef MAPPED
                // sv2v packs array index 0 at the most significant end.
                unsigned bit = (23 - i) * 18, word = bit / 32, shift = bit % 32;
                unsigned long long wide = dut.coefficient_out[word];
                if (shift + 18 > 32)
                    wide |= static_cast<unsigned long long>(dut.coefficient_out[word + 1]) << 32;
                actual[i + 3] = (wide >> shift) & 262143;
#else
                actual[i + 3] = dut.coefficient_out[i];
#endif
            }
#else
            ++_system_clock;
            dut._work(values[5]);
            dut._strobe();
            actual[0] = dut.sample_out();
            actual[1] = dut.valid_out();
            actual[2] = dut.fault_out();
            for (unsigned i = 0; i < 24; ++i)
                actual[i + 3] = dut.coefficient_out[i]();
#endif
            for (unsigned i = 0; i < actual.size(); ++i) {
                if (actual[i] != expected[i])
                    throw std::runtime_error("filter row " + std::to_string(row) + " field " + std::to_string(i) +
                                             " got " + std::to_string(actual[i]) + " expected " +
                                             std::to_string(expected[i]));
                if (trace)
                    trace << (i ? "," : "") << actual[i];
            }
            if (trace)
                trace << '\n';
            ++row;
        }
        if (row < 5000)
            throw std::runtime_error("insufficient filter coverage");
        std::cout << row << " independently checked fixed-point filter states\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
