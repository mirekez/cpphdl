#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VAutonomousReceiveFilter.h"
using DUT = VAutonomousReceiveFilter;
#else
#include "../autonomous_receive_filter.hpp"
using DUT = AutonomousReceiveFilter;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected event trace path");
        std::ofstream trace(argv[1]);
        if (!trace)
            throw std::runtime_error("cannot open autonomous filter trace");
        unsigned events = 0, checked_total = 0, negative_errors = 0;
        for (unsigned trial = 0; trial < 9; ++trial) {
            unsigned rng = trial + 1, sample = 128, local = 1170, mode = 1;
            auto random = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
            DUT dut;
#ifndef VERILATOR
            dut.sample_in = _ASSIGN(sample);
            dut.local_in = _ASSIGN(local);
            dut.mode_in = _ASSIGN(mode);
            dut.advance_in = _ASSIGN(true);
            dut._assign();
#endif
            unsigned output = 128;
            bool valid = false, fault = false;
            std::array<unsigned, 24> coefficients{}, held{};
            auto step = [&](bool reset) {
#ifdef VERILATOR
                dut.clk = 0;
                dut.reset = reset;
                dut.sample_in = sample;
                dut.local_in = local;
                dut.mode_in = mode;
                dut.advance_in = true;
                dut.eval();
                dut.clk = 1;
                dut.eval();
                output = dut.sample_out; valid = dut.valid_out; fault = dut.fault_out;
                for (unsigned i = 0; i < coefficients.size(); ++i)
                    coefficients[i] = dut.coefficient_out[i];
#else
                ++_system_clock;
                dut._work(reset);
                dut._strobe();
                ++_system_clock;
                output = unsigned(dut.sample_out()); valid = dut.valid_out(); fault = dut.fault_out();
                for (unsigned i = 0; i < coefficients.size(); ++i)
                    coefficients[i] = unsigned(dut.coefficient_out[i]());
#endif
                trace << events++ << ',' << output << ',' << valid << ',' << fault;
                for (auto value : coefficients)
                    trace << ',' << value;
                trace << '\n';
                if (fault)
                    throw std::runtime_error("autonomous filter fault");
            };
            step(true);
            const auto initial = coefficients;
            std::array<int, 4> previous{};
            int previous_remote = 0;
            std::deque<int> expected;
            unsigned errors = 0, checked = 0;
            double squared = 0;
            for (unsigned i = 0; i < 65536; ++i) {
                mode = trial == 8 ? 0 : i < 8192 ? 1 : i < 24576 ? 2 : i < 49152 ? 3 : 0;
                const int remote = i < 24576 ? (int(random() % 3) - 1) * 2 : int(random() % 5) - 2;
                double measured = .75 * remote + .25 * previous_remote;
                local = 0;
                const double gains[4] = {.45, .18, .12, .08};
                for (unsigned lane = 0; lane < 4; ++lane) {
                    const int own = int(random() % 5) - 2;
                    measured += gains[lane] * (.75 * own + .25 * previous[lane]);
                    previous[lane] = own;
                    local |= unsigned(own + 2) << (3 * lane);
                }
                previous_remote = remote;
                sample = unsigned(std::max(0, std::min(255, 128 + int(std::lround(measured * 32)) + int(random() % 5) - 2)));
                expected.push_back(remote);
                step(false);
                // No expected symbol enters the DUT. This queue only scores
                // the output six clocks after the physical sample was admitted.
                if (valid != (i >= 6))
                    throw std::runtime_error("changed autonomous filter latency");
                if (i < 8192)
                    for (unsigned tap = 0; tap < 4; ++tap)
                        if (coefficients[tap] != initial[tap])
                            throw std::runtime_error("blind echo acquisition changed the receive FFE");
                if (i == 50000)
                    held = coefficients;
                if (i > 50000 && coefficients != held)
                    throw std::runtime_error("holdout secretly continued training");
                if (expected.size() > 6) {
                    const int desired = expected.front(); expected.pop_front();
                    if (i > 50000) {
                        const int y = int(output) - 128;
                        const int recovered = y < -48 ? -2 : y < -16 ? -1 : y > 48 ? 2 : y > 16 ? 1 : 0;
                        errors += desired != recovered;
                        ++checked;
                        squared += std::pow(double(y - desired * 32), 2);
                    }
                }
            }
            if (checked != 15535)
                throw std::runtime_error("incomplete independent holdout");
            if (trial != 8) {
                if (errors != 0 || std::sqrt(squared / checked) >= 3)
                    throw std::runtime_error("autonomous acquisition failed independent channel holdout");
                checked_total += checked;
            } else {
                if (errors < checked / 10 || coefficients != initial)
                    throw std::runtime_error("disabled-adaptation negative control is ineffective");
                negative_errors = errors;
            }
        }
        if (!trace)
            throw std::runtime_error("incomplete autonomous filter evidence");
        std::cout << "PASS 8 autonomous channel cases, " << checked_total << " error-free held-out symbols, "
                  << negative_errors << " disabled-adaptation errors, " << events << " complete states\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
