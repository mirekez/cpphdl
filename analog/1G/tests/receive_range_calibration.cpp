#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VReceiveRangeCalibration.h"
using DUT = VReceiveRangeCalibration;
#else
#include "../receive_range_calibration.hpp"
using DUT = ReceiveRangeCalibration;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("expected range calibration vectors and trace");
        std::ifstream input(argv[1]);
        std::ofstream trace(argv[2]);
        if (!input || !trace)
            throw std::runtime_error("cannot open range calibration vectors or trace");
        DUT dut;
        std::array<unsigned, 6> inputs{};
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(inputs[1] != 0);
        dut.start_in = _ASSIGN(inputs[2] != 0);
        dut.centre_in = _ASSIGN(inputs[3]);
        dut.span_in = _ASSIGN(inputs[4]);
        dut.target_span_in = _ASSIGN(inputs[5]);
        dut._assign();
#endif
        unsigned row = 0;
        while (input >> inputs[0]) {
            for (unsigned i = 1; i < inputs.size(); ++i)
                if (!(input >> inputs[i]))
                    throw std::runtime_error("truncated range calibration input");
            std::array<unsigned, 5> expected{}, actual{};
            for (auto& value : expected)
                if (!(input >> value))
                    throw std::runtime_error("truncated range calibration expectation");
#ifdef VERILATOR
            dut.reset = inputs[0];
            dut.enable_in = inputs[1];
            dut.start_in = inputs[2];
            dut.centre_in = inputs[3];
            dut.span_in = inputs[4];
            dut.target_span_in = inputs[5];
            dut.clk = 0;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            actual = {unsigned(dut.offset_out), unsigned(dut.gain_out), unsigned(dut.ready_out),
                      unsigned(dut.busy_out), unsigned(dut.fault_out)};
#else
            ++_system_clock;
            dut._work(inputs[0]);
            dut._strobe();
            actual = {unsigned(dut.offset_out()), unsigned(dut.gain_out()), unsigned(dut.ready_out()),
                      unsigned(dut.busy_out()), unsigned(dut.fault_out())};
#endif
            if (actual != expected) {
                std::cerr << "actual:";
                for (auto value : actual)
                    std::cerr << ' ' << value;
                std::cerr << " expected:";
                for (auto value : expected)
                    std::cerr << ' ' << value;
                std::cerr << '\n';
                throw std::runtime_error("range calibration mismatch at row " + std::to_string(row));
            }
            for (unsigned i = 0; i < actual.size(); ++i)
                trace << (i ? "," : "") << actual[i];
            trace << '\n';
            ++row;
        }
        if (row < 10000)
            throw std::runtime_error("insufficient range calibration coverage");
        std::cout << row << " independently checked range calibration clocks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
