#include "receive_reference.hpp"
#include <iostream>
#include <deque>
#ifdef VERILATOR
#ifdef PIPELINED_METRICS
#include "VPipelinedPCSBranchMetrics.h"
using DUT = VPipelinedPCSBranchMetrics;
#else
#include "VPCSBranchMetrics.h"
using DUT = VPCSBranchMetrics;
#endif
#else
#ifdef PIPELINED_METRICS
#include "../pcs_branch_metrics_pipeline.hpp"
using DUT = PipelinedPCSBranchMetrics;
#else
#include "../pcs_receive.hpp"
using DUT = PCSBranchMetrics;
#endif
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected fixture directory [trace.csv]");
        ConstellationReference oracle(argv[1]);
        std::ofstream trace;
        if (argc == 3) {
            trace.open(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open branch metric trace");
        }
        DUT dut;
        unsigned samples = 0, signs = 0, cycles = 0;
        bool advance = true;
#ifndef VERILATOR
        dut.samples_in = _ASSIGN(samples);
        dut.negate_in = _ASSIGN(signs);
        dut.advance_in = _ASSIGN(advance);
        dut._assign();
#endif
        std::array<unsigned, 8> expected_metrics{}, expected_points{};
        struct Expectation {
            std::array<unsigned, 8> metric{}, point{};
            bool valid = false;
        };
#ifdef PIPELINED_METRICS
        constexpr unsigned delay = 3;
#else
        constexpr unsigned delay = 0;
#endif
        std::deque<Expectation> pipeline(delay);
        auto step = [&](std::array<int, 4> values, bool reset = false) {
            samples = 0;
            for (unsigned lane = 0; lane < 4; ++lane)
                samples |= unsigned(values[lane]) << (8 * lane);
            Expectation expected;
            if (reset) {
                expected_metrics.fill(0);
                expected_points.fill(0);
                pipeline.assign(delay, Expectation{});
            } else {
                Expectation current;
                current.valid = advance;
                if (advance)
                    oracle.nearest(values, signs, current.metric, current.point);
                pipeline.push_back(current);
                expected = pipeline.front();
                pipeline.pop_front();
                if (expected.valid) {
                    expected_metrics = expected.metric;
                    expected_points = expected.point;
                }
            }
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.samples_in = samples;
            dut.negate_in = signs;
            dut.advance_in = advance;
            dut.eval();
            dut.clk = 1;
            dut.eval();
#ifdef MAPPED
            // sv2v packs unpacked [0:7] ports with element zero at the MSB.
            auto packed_metric = [&](unsigned subset) {
                unsigned offset = (7 - subset) * 18;
                unsigned long bits = dut.metric_out[offset / 32];
                if (offset % 32 + 18 > 32)
                    bits |= (unsigned long)dut.metric_out[offset / 32 + 1] << 32;
                return unsigned(bits >> (offset % 32)) & 262143;
            };
#define METRIC(i) packed_metric(i)
#define POINT(i) unsigned((dut.point_out >> ((7 - (i)) * 6)) & 63)
#else
#define METRIC(i) unsigned(dut.metric_out[i])
#define POINT(i) unsigned(dut.point_out[i])
#endif
            bool valid = dut.valid_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
#define METRIC(i) unsigned(dut.metric_out[i]())
#define POINT(i) unsigned(dut.point_out[i]())
            bool valid = dut.valid_out();
#endif
            if (valid != expected.valid)
                throw std::runtime_error("branch metric valid/hold/reset mismatch");
            for (unsigned subset = 0; subset < 8; ++subset) {
                unsigned metric = METRIC(subset), point = POINT(subset);
                if (trace.is_open())
                    trace << metric << ',' << point << ',';
                if (metric != expected_metrics[subset] || point != expected_points[subset])
                    throw std::runtime_error("nearest point mismatch at cycle " + std::to_string(cycles) + " subset " +
                                             std::to_string(subset));
            }
            if (trace.is_open())
                trace << valid << '\n';
            ++cycles;
        };
        step({128, 128, 128, 128}, true);
        unsigned random = 0x1BA5E;
        // Every exact point, all polarity combinations; low additive noise
        // covers actual ADC values around each decision region.
        for (unsigned subset = 0; subset < 8; ++subset)
            for (unsigned point = 0; point < 64; ++point)
                for (signs = 0; signs < 16; ++signs) {
                    std::array<int, 4> sample{};
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        int level = oracle.symbols[subset][point][lane];
                        sample[lane] = 128 + ((signs >> lane) & 1 ? -level : level) * 32;
                    }
                    step(sample);
                }
        // Include full-scale extremes, exact half-level ties and unrelated ADC
        // samples: the optimized search must equal exhaustive 512-point search.
        for (unsigned test = 0; test < 8192; ++test) {
            std::array<int, 4> sample{};
            for (auto& value : sample)
                value = next_random(random) & 255;
            signs = next_random(random) & 15;
            advance = test % 13 != 0;
            step(sample);
            if (test % 127 == 0)
                step(sample, true);
        }
        advance = true;
        for (unsigned value = 0; value < 256; ++value)
            step({int(value), int(value), int(value), int(value)});
        advance = false;
        for (unsigned i = 0; i < delay; ++i)
            step({128, 128, 128, 128});
        std::cout << cycles << " nearest-point checks against exhaustive IEEE constellations passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
