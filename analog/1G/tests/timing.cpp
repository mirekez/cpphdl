#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#include "VReceiveTimingLoop.h"
using DUT = VReceiveTimingLoop;
#else
#include "../receive_timing.hpp"
using DUT = ReceiveTimingLoop;
long _system_clock = -1;
#endif

namespace {
constexpr double unit = 16777216.;
struct Clock {
    DUT dut;
    std::array<unsigned, 6> input{};
    Clock() {
#ifndef VERILATOR
        dut.early_in = [&] { return u<8>(input[0]); };
        dut.middle_in = [&] { return u<8>(input[1]); };
        dut.late_in = [&] { return u<8>(input[2]); };
        dut.advance_in = [&] { return input[3] != 0; };
        dut.track_in = [&] { return input[4] != 0; };
#endif
    }
    std::array<unsigned, 6> tick() {
#ifdef VERILATOR
        dut.early_in = input[0];
        dut.middle_in = input[1];
        dut.late_in = input[2];
        dut.advance_in = input[3];
        dut.track_in = input[4];
        dut.reset = input[5];
        dut.clk = 0;
        dut.eval();
        dut.clk = 1;
        dut.eval();
        return {dut.interval_out, dut.period_out, dut.phase_out, dut.error_out, dut.activity_out, dut.valid_out};
#else
        ++_system_clock;
        dut._work(input[5]);
        dut._strobe();
        return {unsigned(dut.interval_out()), unsigned(dut.period_out()),   unsigned(dut.phase_out()),
                unsigned(dut.error_out()),    unsigned(dut.activity_out()), unsigned(dut.valid_out())};
#endif
    }
};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void closed_loop(double ppm, double initial, std::ostream* trace) {
    unsigned random = 0x93BA5E;
    auto next_random = [&] {
        random = random * 1664525u + 1013904223u;
        return random;
    };
    std::vector<int> symbols(25000);
    for (auto& symbol : symbols)
        symbol = int(next_random() % 5) - 2;
    const double source_period = 1 + ppm * 1e-6;
    auto coordinates = [&](double time) { return time / source_period - .65 - .006 * std::sin(time / 97.); };
    auto sample = [&](double time) {
        const double index = coordinates(time);
        const unsigned before = unsigned(std::floor(index));
        const double fraction = index - before;
        double value = symbols.at(before) * (1 - fraction) + symbols.at(before + 1) * fraction;
        value += (int(next_random() % 2001) - 1000) * .000008;
        return unsigned(std::clamp(std::floor(value * 32 + .5) + 128, 0., 255.));
    };
    Clock clock;
    clock.input = {128, 128, 128, 1, 1, 1};
    clock.tick();
    double time = 20 + initial, period = 1, phase_error = 0, untracked_error = 0;
    unsigned transitions = 0, errors = 0;
    for (unsigned i = 0; i < 20000; ++i) {
        const unsigned middle = sample(time);
        clock.input = {sample(time - period / 2), middle, sample(time + period / 2), 1, 1, 0};
        auto output = clock.tick();
        require(output[5] == unsigned(i >= 6), "timing pipeline transaction disappeared");
        if (trace)
            *trace << ppm << ',' << i << ',' << output[0] << ',' << output[1] << ',' << output[2] << ',' << output[3]
                   << ',' << output[4] << '\n';
        if (i >= 16000) {
            double cycles = coordinates(time);
            phase_error += std::abs(cycles - std::round(cycles));
            double untracked = coordinates(20 + initial + i);
            untracked_error += std::abs(untracked - std::round(untracked));
            errors += std::clamp(int(std::floor((int(middle) - 128) / 32. + .5)), -2, 2) !=
                      symbols.at(unsigned(std::round(cycles)));
        }
        transitions += output[4];
        time += output[0] / unit; // Close feedback using this implementation's actual output.
        period = output[1] / unit;
    }
    phase_error /= 4000;
    untracked_error /= 4000;
    std::cout << "source " << ppm << " ppm: phase=" << phase_error << " UI, open=" << untracked_error
              << " UI, recovered=" << (period - 1) * 1e6 << " ppm, errors=" << errors << '\n';
    require(phase_error < .08 && phase_error < untracked_error * .5 && errors == 0 && transitions > 8000,
            "data-derived loop did not recover noisy PAM5 timing");
    require(std::abs(period - source_period) < .0003, "recovered period missed source frequency");
    // Explicit holdover continues accumulated phase at the retained frequency,
    // even when adversarial samples would saturate a tracking integrator.
    clock.input = {255, 255, 0, 1, 0, 0};
    for (unsigned i = 0; i < 7; ++i)
        clock.tick(); // Drain older tracking observations before scoring holdover.
    const auto saved = clock.tick();
    for (unsigned i = 0; i < 100; ++i) {
        clock.input = {255, 255, 0, 1, 0, 0};
        const auto output = clock.tick();
        require(output[0] == saved[1] && output[1] == saved[1], "holdover changed frequency");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3, "usage: timing fixture [trace]");
        std::ifstream input(argv[1]);
        require(bool(input), "missing timing fixture");
        std::ofstream trace;
        if (argc == 3)
            trace.open(argv[2]);
        Clock clock;
        unsigned rows = 0;
        while (input >> clock.input[0]) {
            for (unsigned i = 1; i < 6; ++i)
                require(bool(input >> clock.input[i]), "truncated timing inputs");
            std::array<unsigned, 6> expected{};
            for (auto& value : expected)
                require(bool(input >> value), "truncated timing expectations");
            const auto actual = clock.tick();
            if (actual != expected)
                throw std::runtime_error("timing contract differs at row " + std::to_string(rows));
            if (trace)
                for (unsigned i = 0; i < actual.size(); ++i)
                    trace << actual[i] << (i + 1 == actual.size() ? '\n' : ',');
            ++rows;
        }
        require(rows > 16000, "insufficient timing coverage");
        for (double ppm : {-1500., -500., -100., 100., 500., 1500.})
            for (double phase : {.3, .9})
                closed_loop(ppm, phase, trace.is_open() ? &trace : nullptr);
        std::cout << rows << " independent states and 12 closed-loop noisy PAM5 cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
