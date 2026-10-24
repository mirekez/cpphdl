#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VReceiveAnalogGainControl.h"
using DUT = VReceiveAnalogGainControl;
#else
#include "../receive_gain_control.hpp"
using DUT = ReceiveAnalogGainControl;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("expected gain control vectors and trace");
        std::ifstream input(argv[1]);
        std::ofstream trace(argv[2]);
        if (!input || !trace)
            throw std::runtime_error("cannot open gain control vectors or trace");
        DUT dut;
        std::array<unsigned, 9> inputs{};
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(inputs[1] != 0);
        dut.restart_in = _ASSIGN(inputs[2] != 0);
        dut.training_in = _ASSIGN(inputs[3] != 0);
        dut.advance_in = _ASSIGN(inputs[4] != 0);
        dut.adc_fault_in = _ASSIGN(inputs[5] != 0);
        dut.sample_in = _ASSIGN(inputs[6]);
        dut.minimum_span_in = _ASSIGN(inputs[7]);
        dut.maximum_span_in = _ASSIGN(inputs[8]);
        dut._assign();
#endif
        unsigned row = 0;
        while (input >> inputs[0]) {
            for (unsigned i = 1; i < inputs.size(); ++i)
                if (!(input >> inputs[i]))
                    throw std::runtime_error("truncated gain control input");
            std::array<unsigned, 10> expected{}, actual{};
            for (auto& value : expected)
                if (!(input >> value))
                    throw std::runtime_error("truncated gain control expectation");
#ifdef VERILATOR
            dut.reset = inputs[0];
            dut.enable_in = inputs[1];
            dut.restart_in = inputs[2];
            dut.training_in = inputs[3];
            dut.advance_in = inputs[4];
            dut.adc_fault_in = inputs[5];
            dut.sample_in = inputs[6];
            dut.minimum_span_in = inputs[7];
            dut.maximum_span_in = inputs[8];
            dut.clk = 0;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            actual = {unsigned(dut.select_n_out), unsigned(dut.level_out), unsigned(dut.ready_out), unsigned(dut.changed_out), unsigned(dut.weak_out), unsigned(dut.overload_out), unsigned(dut.fault_out), unsigned(dut.span_out), unsigned(dut.centre_out), unsigned(dut.settled_out)};
#else
            ++_system_clock;
            dut._work(inputs[0]);
            dut._strobe();
            actual = {unsigned(dut.select_n_out()), unsigned(dut.level_out()), unsigned(dut.ready_out()), unsigned(dut.changed_out()), unsigned(dut.weak_out()), unsigned(dut.overload_out()), unsigned(dut.fault_out()), unsigned(dut.span_out()), unsigned(dut.centre_out()), unsigned(dut.settled_out())};
#endif
            if (actual != expected) {
                std::cerr << "actual:";
                for (auto value : actual)
                    std::cerr << ' ' << value;
                std::cerr << " expected:";
                for (auto value : expected)
                    std::cerr << ' ' << value;
                std::cerr << '\n';
                throw std::runtime_error("transmit gain control mismatch at row " + std::to_string(row));
            }
            for (unsigned i = 0; i < actual.size(); ++i)
                trace << (i ? "," : "") << actual[i];
            trace << '\n';
            ++row;
        }
        if (row < 10000)
            throw std::runtime_error("insufficient gain control coverage");
        std::cout << row << " independently checked gain control clocks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
