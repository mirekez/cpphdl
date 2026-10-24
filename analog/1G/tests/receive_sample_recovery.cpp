#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#include "VReceiveSampleRecovery.h"
using DUT = VReceiveSampleRecovery;
#else
#include "../receive_sample_recovery.hpp"
using DUT = ReceiveSampleRecovery;
long _system_clock = -1;
#endif

namespace {
constexpr double Q = 16777216.;
struct Clock {
    DUT dut;
    unsigned word = 0x80808080, tag = 0, valid = 0, track = 1, restart = 0, seed = 0, origin = 0;
    Clock() {
#ifndef VERILATOR
        dut.samples_in = [&] { return u<32>(word); };
        dut.tag_in = [&] { return u<16>(tag); };
        dut.valid_in = [&] { return valid != 0; };
        dut.track_in = [&] { return track != 0; };
        dut.restart_in = [&] { return restart != 0; };
        dut.initial_phase_in = [&] { return u<25>(seed); };
        dut.origin_in = [&] { return u<16>(origin); };
        dut._assign();
#endif
    }
    std::array<unsigned, 10> tick(bool reset = false) {
#ifdef VERILATOR
        dut.samples_in = word;
        dut.tag_in = tag;
        dut.valid_in = valid;
        dut.track_in = track;
        dut.restart_in = restart;
        dut.initial_phase_in = seed;
        dut.origin_in = origin;
        dut.reset = reset;
        dut.clk = 0;
        dut.eval();
        dut.clk = 1;
        dut.eval();
        return {dut.samples_out, dut.tag_out,      dut.fraction_out, dut.valid_out, dut.fault_out,
                dut.clipped_out, dut.interval_out, dut.period_out,   dut.error_out, dut.correction_valid_out};
#else
        ++_system_clock;
        dut._work(reset);
        dut._strobe();
        return {unsigned(dut.samples_out()),         unsigned(dut.tag_out()),    unsigned(dut.fraction_out()),
                unsigned(dut.valid_out()),           unsigned(dut.fault_out()),  unsigned(dut.clipped_out()),
                unsigned(dut.interval_out()),        unsigned(dut.period_out()), unsigned(dut.error_out()),
                unsigned(dut.correction_valid_out())};
#endif
    }
};
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
double pulse(double t) {
    constexpr double pi = 3.14159265358979323846, beta = .35;
    if (std::abs(t) < 1e-12)
        return 1;
    if (std::abs(std::abs(t) - 1 / (2 * beta)) < 1e-10)
        return (pi / 4) * std::sin(pi / (2 * beta)) / (pi / (2 * beta));
    return std::sin(pi * t) / (pi * t) * std::cos(pi * beta * t) / (1 - 4 * beta * beta * t * t);
}
void emit(std::ofstream& trace, unsigned test, unsigned cycle, const Clock& c, const std::array<unsigned, 10>& out,
          bool reset = false) {
    if (!trace)
        return;
    trace << test << ',' << cycle << ',' << c.word << ',' << c.tag << ',' << c.valid << ',' << c.track << ','
          << c.restart << ',' << c.seed << ',' << c.origin << ',' << reset;
    for (auto v : out)
        trace << ',' << v;
    trace << '\n';
}

void closed_loop(unsigned test, double ppm, double delay, unsigned seed, unsigned origin, std::ofstream& trace) {
    constexpr unsigned count = 42000, transport = 27;
    const double period = 1 + ppm * 1e-6;
    std::array<std::vector<int>, 4> symbols;
    unsigned random = 0x93BA5E;
    for (auto& lane : symbols) {
        lane.resize(count / 2 + 100);
        for (auto& s : lane) {
            random = random * 1664525u + 1013904223u;
            s = int(random % 5) - 2;
        }
    }
    // Immutable uniform-rate inputs are constructed before the DUT runs.
    // No output phase/interval controls the waveform generator or sample times.
    std::vector<unsigned> samples(count);
    for (unsigned n = 0; n < count; ++n) {
        const double coordinate = (20 + n * .5 + delay) / period - .65;
        const int center = int(std::floor(coordinate));
        for (unsigned lane = 0; lane < 4; ++lane) {
            double value = 0;
            for (int k = center - 8; k <= center + 8; ++k)
                value += symbols[lane].at(k) * pulse(coordinate - k);
            const unsigned code = unsigned(std::clamp(std::floor(128 + 32 * value + .5), 0., 255.));
            samples[n] |= code << (8 * lane);
        }
    }
    Clock clock;
    clock.seed = seed;
    clock.origin = origin;
    emit(trace, test, 4294967295u, clock, clock.tick(true), true);
    unsigned previous_tag = origin, unwrapped = 0, outputs = 0, scored = 0, errors = 0, slips = 0;
    int previous_symbol = -1;
    double phase_error = 0, maximum_phase = 0, maximum_arithmetic = 0;
    std::array<unsigned, 10> out{};
    for (unsigned cycle = 0; cycle < count + transport; ++cycle) {
        clock.valid = cycle >= transport;
        if (clock.valid) {
            const unsigned n = cycle - transport;
            clock.word = samples[n];
            clock.tag = (origin + n) & 65535;
        }
        out = clock.tick();
        emit(trace, test, cycle, clock, out);
        require(!out[4], "contiguous input caused a recovery fault");
        if (!out[3])
            continue;
        const unsigned delta = (out[1] - previous_tag) & 65535;
        require(outputs == 0 ? delta <= 6 : delta >= 1 && delta <= 3,
                "unbounded target advance or wrapped history error");
        unwrapped += delta;
        previous_tag = out[1];
        ++outputs;
        require(cycle == transport + unwrapped + 8, "interpolator latency/tag changed");
        const double f = out[2] / Q;
        for (unsigned lane = 0; lane < 4; ++lane) {
            double x[4];
            for (unsigned k = 0; k < 4; ++k)
                x[k] = (samples.at(unwrapped - 1 + k) >> (8 * lane)) & 255;
            const double exact =
                x[1] +
                .5 * f *
                    (x[2] - x[0] + f * (2 * x[0] - 5 * x[1] + 4 * x[2] - x[3] + f * (3 * (x[1] - x[2]) + x[3] - x[0])));
            maximum_arithmetic = std::max(maximum_arithmetic,
                                          std::abs(double((out[0] >> (8 * lane)) & 255) - std::clamp(exact, 0., 255.)));
        }
        if (unwrapped < 32000)
            continue;
        const double coordinate = (20 + (unwrapped + f) * .5 + delay) / period - .65;
        const int symbol = int(std::round(coordinate));
        const double error = std::abs(coordinate - symbol);
        phase_error += error;
        maximum_phase = std::max(maximum_phase, error);
        ++scored;
        if (previous_symbol >= 0 && symbol != previous_symbol + 1)
            ++slips;
        previous_symbol = symbol;
        for (unsigned lane = 0; lane < 4; ++lane) {
            const int level = std::clamp(int(std::floor((int((out[0] >> (8 * lane)) & 255) - 128) / 32. + .5)), -2, 2);
            errors += level != symbols[lane].at(symbol);
        }
    }
    std::cout << "case " << test << " ppm=" << ppm << " delay_UI=" << delay << " phase=" << phase_error / scored
              << " maxphase=" << maximum_phase << " errors=" << errors << " slips=" << slips
              << " arithmetic_counts=" << maximum_arithmetic << " period_ppm=" << (out[7] / Q - 1) * 1e6 << '\n';
    require(scored > 4000, "insufficient recovered symbols");
    require(maximum_arithmetic <= 2, "fixed-point cubic exceeds two-count arithmetic allowance");
    require(phase_error / scored < .08 && maximum_phase < .20 && errors == 0 && slips == 0,
            "fixed-sample timing recovery did not recover source symbols");
    require(std::abs(out[7] / Q - period) < .0003, "recovered period missed source rate");
}

void faults(std::ofstream& trace) {
    for (unsigned kind = 0; kind < 3; ++kind) {
        Clock clock;
        emit(trace, 100 + kind, 4294967295u, clock, clock.tick(true), true);
        clock.valid = 1;
        for (unsigned n = 0; n < 32; ++n) {
            clock.tag = n;
            if (n == 12)
                clock.tag = kind == 0 ? n + 1 : kind == 1 ? n - 1 : n - 2;
            const auto out = clock.tick();
            emit(trace, 100 + kind, n, clock, out);
            require(n < 12 ? !out[4] : out[4] && !out[3], "missing/duplicate/out-of-order input was not contained");
        }
        clock.origin = 65530;
        clock.restart = 1;
        emit(trace, 110 + kind, 4294967295u, clock, clock.tick());
        clock.restart = 0;
        for (unsigned n = 0; n < 24; ++n) {
            clock.tag = (65530 + n) & 65535;
            const auto out = clock.tick();
            emit(trace, 110 + kind, n, clock, out);
            require(!out[4], "explicit restart failed to reacquire tagged history");
            if (n < 12)
                require(!out[3], "pending output escaped reset/restart");
        }
    }
    Clock pending;
    emit(trace, 120, 4294967295u, pending, pending.tick(true), true);
    pending.valid = 1;
    for (unsigned n = 0; n < 9; ++n) {
        pending.tag = n;
        emit(trace, 120, n, pending, pending.tick());
    }
    auto out = pending.tick(true);
    emit(trace, 120, 9, pending, out, true);
    require(!out[3] && !out[4], "reset failed with pending interpolation");
    pending.valid = 0;
    for (unsigned n = 0; n < 12; ++n) {
        out = pending.tick();
        emit(trace, 120, 10 + n, pending, out);
        require(!out[3], "stale pipeline output after reset");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        std::ofstream trace;
        if (argc > 1 && std::string(argv[1]) != "none")
            trace.open(argv[1]);
        unsigned test = 0;
        for (double ppm : {-1500., 0., 1500.})
            for (double delay : {0., .105, .35, .9}) {
                closed_loop(test, ppm, delay, (test % 2) ? 10066330 : 0, test % 2 ? 65520 : 0, trace);
                ++test;
            }
        faults(trace);
        std::cout << "PASS fixed-sample recovery and fault/restart checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
