#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#include "VReceiveTimingConfidence.h"
using DUT = VReceiveTimingConfidence;
#else
#include "../receive_timing_confidence.hpp"
using DUT = ReceiveTimingConfidence;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected full-clock trace");
        std::ofstream trace(argv[1]);
        DUT dut;
        int error = 0;
        unsigned period = 16777216, clocks = 0, windows = 0;
        bool valid = false, fault = false, expected = false;
        std::vector<int> errors;
        std::vector<unsigned> periods;
#ifndef VERILATOR
        dut.error_in = _ASSIGN(u<14>(error));
        dut.period_in = _ASSIGN(u<25>(period));
        dut.valid_in = _ASSIGN(valid);
        dut.fault_in = _ASSIGN(fault);
        dut._assign();
#endif
        auto tick = [&](bool reset = false) {
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.error_in = unsigned(error) & 16383;
            dut.period_in = period;
            dut.valid_in = valid;
            dut.fault_in = fault;
            dut.eval();
            const bool ready = dut.ready_out;
#else
            ++_system_clock;
            const bool ready = dut.ready_out();
#endif
            if (ready != (expected && !fault))
                throw std::runtime_error("timing confidence differs from complete-window oracle");
            trace << clocks++ << ',' << error << ',' << period << ',' << valid << ',' << fault << ',' << reset << ','
                  << ready << '\n';
            if (reset || fault) {
                errors.clear();
                periods.clear();
                expected = false;
            } else if (valid) {
                errors.push_back(error);
                periods.push_back(period);
                if (errors.size() == 1024) {
                    int total = 0, magnitude = 0, excited = 0;
                    bool subwindows = true;
                    for (unsigned j = 0; j < 8; ++j) {
                        int sum = 0;
                        for (unsigned k = 0; k < 128; ++k) {
                            int e = errors[j * 128 + k];
                            sum += e;
                            magnitude += e < 0 ? -e : e;
                            excited += e != 0;
                        }
                        total += sum;
                        subwindows &= sum >= -27487 && sum <= 27487;
                    }
                    int slew = int(periods.back()) - int(periods.front());
                    expected = subwindows && total >= -27487 && total <= 27487 && magnitude <= 2097152 &&
                               excited >= 128 && slew >= -537 && slew <= 537;
                    errors.clear();
                    periods.clear();
                    ++windows;
                }
            }
#ifdef VERILATOR
            dut.clk = 1;
            dut.eval();
#else
            dut._work(reset);
            dut._strobe();
#endif
        };
        tick(true);
        for (unsigned scenario = 0; scenario < 12; ++scenario) {
            valid = false;
            tick(true);
            for (unsigned i = 0; i < 1024; ++i) {
                valid = false;
                error = -8192;
                period = 1;
                for (unsigned gap = 0; gap < i % 3; ++gap)
                    tick();
                valid = true;
                period = 16777216;
                error = (i & 1) ? 512 : -512;
                if (scenario == 1)
                    error = 0;
                if (scenario == 2)
                    error = 32;
                if (scenario == 3)
                    period += i == 1023 ? 538 : 0;
                if (scenario == 4)
                    error = (i / 128 & 1) ? 256 : -256;
                if (scenario == 5)
                    error = (i & 1) ? 4096 : -4096;
                if (scenario == 6)
                    error = i < 127 ? ((i & 1) ? 16 : -16) : 0;
                if (scenario == 7)
                    error = i < 128 ? ((i & 1) ? 16 : -16) : 0;
                if (scenario == 8)
                    error = (i & 1) ? 2048 : -2048;
                if (scenario == 9)
                    error = (i & 1) ? 2049 : -2049;
                if (scenario == 10)
                    period += i == 1023 ? 537 : 0;
                if (scenario == 11)
                    period -= i == 1023 ? 537 : 0;
                tick();
            }
            valid = false;
            tick();
            const bool wanted = scenario == 0 || scenario == 7 || scenario == 8 || scenario >= 10;
            if (expected != wanted)
                throw std::runtime_error("declared acceptance/rejection scenario incorrect");
            fault = true;
            tick();
            fault = false;
            tick();
            valid = true;
            for (unsigned i = 0; i < 317; ++i)
                tick();
            valid = false;
            tick(true);
        }
        std::cout << "PASS " << clocks << " clocks, " << windows
                  << " complete windows; excitation, cancellation, drift, pauses, reset and immediate fault\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
