#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef VERILATOR
#include "VReceiveSamplePCSQualified.h"
using DUT = VReceiveSamplePCSQualified;
#else
#include "../receive_sample_pcs_qualified.hpp"
using DUT = ReceiveSamplePCSQualified;
long _system_clock = -1;
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition)
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
void run(const std::string& file, double ppm, double delay, unsigned test, std::ofstream& trace) {
    std::ifstream input(file);
    unsigned master, count, frame_count;
    require(bool(input >> master >> count >> frame_count), "missing independent symbols");
    std::vector<unsigned> symbols(count);
    for (auto& symbol : symbols)
        require(bool(input >> symbol), "short symbol source");
    std::vector<std::vector<unsigned>> expected(frame_count);
    for (auto& frame : expected) {
        unsigned size;
        require(bool(input >> size), "missing frame truth");
        frame.resize(size);
        for (auto& byte : frame)
            require(bool(input >> byte), "short frame truth");
    }
    // The entire ADC input is fixed before construction or execution of the DUT.
    const double period = 1 + ppm * 1e-6;
    const unsigned sample_count = unsigned((count - 40) * 2 * period);
    std::vector<unsigned> samples(sample_count);
    for (unsigned n = 0; n < sample_count; ++n) {
        const double coordinate = (20 + n * .5 + delay) / period - .65;
        const int center = int(std::floor(coordinate));
        for (unsigned lane = 0; lane < 4; ++lane) {
            double value = 0;
            for (int k = center - 8; k <= center + 8; ++k)
                value += (int((symbols.at(k) >> (3 * lane)) & 7) - 2) * pulse(coordinate - k);
            samples[n] |= unsigned(std::clamp(std::floor(128 + 32 * value + .5), 0., 255.)) << (8 * lane);
        }
    }
    DUT dut;
    unsigned word = 0, tag = 0;
    bool valid = false, pma = false;
#ifndef VERILATOR
    dut.samples_in = _ASSIGN(word);
    dut.tag_in = _ASSIGN(tag);
    dut.valid_in = _ASSIGN(valid);
    dut.restart_in = _ASSIGN(false);
    dut.initial_phase_in = _ASSIGN(u<25>(0));
    dut.origin_in = _ASSIGN(u<16>(65520));
    dut.remote_master_in = _ASSIGN(master != 0);
    dut.pma_ok_in = _ASSIGN(pma);
    dut._assign();
#endif
    std::vector<unsigned> frame;
    unsigned completed = 0, bytes = 0, locked_clocks = 0, idle_errors = 0;
    for (unsigned cycle = 0; cycle <= sample_count; ++cycle) {
        const bool reset = cycle == 0;
        valid = !reset;
        if (valid) {
            word = samples[cycle - 1];
            tag = (65520 + cycle - 1) & 65535;
        }
        pma = !reset;
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.samples_in = word;
        dut.tag_in = tag;
        dut.valid_in = valid;
        dut.restart_in = false;
        dut.initial_phase_in = 0;
        dut.origin_in = 65520;
        dut.remote_master_in = master;
        dut.pma_ok_in = pma;
        dut.eval();
        const unsigned output = dut.receive_out;
        const bool output_valid = dut.receive_valid_out, fault = dut.fault_out, locked = dut.locked_out;
        const bool quality = dut.quality_out, clipped = dut.clipped_out;
        const bool confident = dut.timing_confident_out, timing_valid = dut.timing_valid_out;
        const unsigned timing_error = dut.timing_error_out, timing_period = dut.timing_period_out;
        const unsigned quality_idles = dut.quality_idles_out, quality_flushed = dut.quality_flushed_out;
        const unsigned symbol_valid = dut.symbol_valid_out, symbol_tag = dut.symbol_tag_out,
                       symbol_fraction = dut.symbol_fraction_out;
#else
        ++_system_clock;
        const unsigned output = unsigned(dut.receive_out());
        const bool output_valid = dut.receive_valid_out(), fault = dut.fault_out(), locked = dut.locked_out();
        const bool quality = dut.quality_out(), clipped = dut.clipped_out();
        const bool confident = dut.timing_confident_out(), timing_valid = dut.timing_valid_out();
        const unsigned timing_error = unsigned(dut.timing_error_out()),
                       timing_period = unsigned(dut.timing_period_out());
        const unsigned quality_idles = unsigned(dut.quality_idles_out()),
                       quality_flushed = unsigned(dut.quality_flushed_out());
        const unsigned symbol_valid = dut.symbol_valid_out(), symbol_tag = unsigned(dut.symbol_tag_out()),
                       symbol_fraction = unsigned(dut.symbol_fraction_out());
#endif
        // Observe the settled state before this edge in both engines. Child
        // ports may already be cached when consumed by a parent during work;
        // mixing post-strobe reads with those cached values is not a boundary.
#ifdef VERILATOR
        dut.clk = 1;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
#endif
        trace << test << ',' << cycle << ',' << word << ',' << tag << ',' << valid << ',' << pma << ',' << reset << ','
              << output << ',' << output_valid << ',' << fault << ',' << locked << ',' << symbol_valid << ','
              << symbol_tag << ',' << symbol_fraction << ',' << quality << ',' << clipped << ',' << quality_idles << ','
              << quality_flushed << ',' << confident << ',' << timing_error << ',' << timing_period << ','
              << timing_valid << '\n';
        require(!fault, "sample history fault in composed receiver");
        locked_clocks += locked;
        if (!output_valid || !locked)
            continue;
        idle_errors += (output & 512) && !(output & 256);
        require(!(output & 512) || !(output & 256), "PCS reported frame receive error");
        if (output & 256) {
            require(quality, "frame arrived without hardware-derived receive quality");
            frame.push_back(output & 255);
        } else if (!frame.empty()) {
            require(completed < expected.size(), "unexpected extra frame");
            if (frame != expected[completed]) {
                std::cerr << "frame " << completed << " actual=" << frame.size()
                          << " expected=" << expected[completed].size() << '\n';
                throw std::runtime_error("decoded frame differs from independent IEEE source");
            }
            bytes += frame.size();
            ++completed;
            frame.clear();
        }
    }
    require(frame.empty() && completed == frame_count, "missing or unfinished decoded frame");
    require(locked_clocks > 1000, "PCS failed acquisition");
    std::cout << "case " << test << " master=" << master << " ppm=" << ppm << " delay_UI=" << delay
              << " idle_errors=" << idle_errors << " frames=" << completed << " bytes=" << bytes << " PASS\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3 || argc == 4, "expected fixture directory trace [pilot]");
        std::ofstream trace(argv[2]);
        require(bool(trace), "cannot write trace");
        unsigned test = 0;
        for (unsigned master = 0; master < 2; ++master)
            for (double ppm : {-1500., 0., 1500.})
                for (double delay : {0., .105, .9}) {
                    run(std::string(argv[1]) + "/role" + std::to_string(master) + ".txt", ppm, delay, test++, trace);
                    if (argc == 4)
                        return 0;
                }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
