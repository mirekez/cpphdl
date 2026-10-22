#include "receive_reference.hpp"
#include <algorithm>
#include <deque>
#include <iostream>
#include <vector>
#ifdef VERILATOR
#include "VPCSTrellisDecoder.h"
using DUT = VPCSTrellisDecoder;
#else
#include "../pcs_receive.hpp"
using DUT = PCSTrellisDecoder;
long _system_clock = -1;
#endif

// Independent full-path dynamic program: enumerate predecessors for each next
// state and derive its two input bits, rather than the DUT's forward branches.
struct TrellisReference {
    std::array<unsigned long, 8> costs{};
    std::array<std::vector<unsigned>, 8> paths;
    std::deque<std::pair<unsigned, bool>> tail;
    unsigned data = 0, state = 0, count = 0;
    bool valid = false;
    void reset() {
        costs.fill(1UL << 50);
        costs[0] = 0;
        for (auto& path : paths)
            path.clear();
        data = state = count = 0;
        valid = false;
        tail.clear();
    }
    void step(const std::array<unsigned, 8>& metric, const std::array<unsigned, 8>& point, bool advance, bool start,
              bool end, bool reset_request) {
        valid = false;
        bool draining = !tail.empty();
        if (draining) {
            data = tail.front().first;
            valid = tail.front().second;
            tail.pop_front();
        }
        if (start || reset_request) {
            costs.fill(1UL << 50);
            costs[0] = 0;
            for (auto& path : paths)
                path.clear();
            state = count = 0;
            if (reset_request || !draining) {
                data = 0;
                valid = false;
            }
            if (reset_request)
                tail.clear();
            return;
        }
        if (!advance)
            return;
        std::array<unsigned long, 8> next{};
        std::array<std::vector<unsigned>, 8> history;
        next.fill(1UL << 50);
        for (unsigned destination = 0; destination < 8; ++destination) {
            for (unsigned previous = 0; previous < 8; ++previous) {
                if ((destination & 1) != previous / 4)
                    continue;
                unsigned sd6 = ((destination >> 1) & 1) ^ (previous & 1);
                unsigned sd7 = (destination >> 2) ^ ((previous >> 1) & 1);
                unsigned subset = 4 * sd6 + 2 * sd7 + (destination & 1);
                unsigned long candidate = costs[previous] + metric[subset];
                if (candidate < next[destination]) {
                    next[destination] = candidate;
                    history[destination] = paths[previous];
                    history[destination].push_back(point[subset] + 64 * sd6 + 128 * sd7);
                    if (history[destination].size() > 32)
                        history[destination].erase(history[destination].begin());
                }
            }
        }
        state = unsigned(std::min_element(next.begin(), next.end()) - next.begin());
        costs = next;
        paths = history;
        ++count;
        if (count >= 32 || !draining) {
            valid = count >= 32;
            data = valid ? paths[state].front() : 0;
        }
        if (end) {
            state = 0;
            if (count >= 32 || !draining) {
                valid = count >= 32;
                data = valid ? paths[0].front() : 0;
            }
            std::vector<unsigned> padded(32 - paths[0].size(), 0);
            unsigned padding = padded.size();
            padded.insert(padded.end(), paths[0].begin(), paths[0].end());
            while (tail.size() < 31)
                tail.emplace_back(0, false);
            for (unsigned index = 1; index < 32; ++index)
                if (index >= padding)
                    tail[index - 1] = {padded[index], true};
        }
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected fixture directory [trace.csv]");
        ConstellationReference constellation(argv[1]);
        TrellisReference oracle;
        DUT dut;
        std::ofstream trace;
        if (argc == 3) {
            trace.open(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open trellis trace");
        }
        bool advance = false, start = false, end = false;
        std::array<unsigned, 8> metric{}, point{};
#ifndef VERILATOR
        dut.advance_in = _ASSIGN(advance);
        dut.start_in = _ASSIGN(start);
        dut.end_in = _ASSIGN(end);
        for (unsigned i = 0; i < 8; ++i) {
            dut.metric_in[i] = _ASSIGN_I(metric[i]);
            dut.point_in[i] = _ASSIGN_I(point[i]);
        }
        dut._assign();
#endif
        unsigned cycles = 0;
        bool observed_valid = false;
        auto step = [&](bool reset = false) {
            unsigned expected_data = reset ? 0 : oracle.data;
            unsigned expected_state = reset ? 0 : oracle.state;
            bool expected_valid = reset ? false : oracle.valid;
            oracle.step(metric, point, advance, start, end, reset);
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.start_in = start;
            dut.end_in = end;
            dut.advance_in = advance;
#ifdef MAPPED
            for (unsigned word = 0; word < 5; ++word)
                dut.metric_in[word] = 0;
            dut.point_in = 0;
            for (unsigned i = 0; i < 8; ++i) {
                unsigned offset = (7 - i) * 18;
                unsigned long shifted = (unsigned long)metric[i] << (offset % 32);
                dut.metric_in[offset / 32] |= unsigned(shifted);
                if (offset % 32 + 18 > 32)
                    dut.metric_in[offset / 32 + 1] |= unsigned(shifted >> 32);
                dut.point_in |= (unsigned long)point[i] << ((7 - i) * 6);
            }
#else
            for (unsigned i = 0; i < 8; ++i) {
                dut.metric_in[i] = metric[i];
                dut.point_in[i] = point[i];
            }
#endif
            dut.eval();
            dut.clk = 1;
            dut.eval();
            unsigned data = dut.data_out, state = dut.state_out;
            bool valid = dut.valid_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            unsigned data = unsigned(dut.data_out()), state = unsigned(dut.state_out());
            bool valid = dut.valid_out();
#endif
            if (trace.is_open())
                trace << data << ',' << valid << ',' << state << '\n';
            observed_valid = valid;
            if (data != expected_data || valid != expected_valid || state != expected_state)
                throw std::runtime_error("trellis/full-path oracle mismatch at cycle " + std::to_string(cycles));
            ++cycles;
            return data;
        };
        step(true);
        unsigned random = 0x4DFA5, hard_errors = 0, soft_errors = 0, clean_errors = 0, overload_errors = 0;
        for (unsigned stream = 0; stream < 12; ++stream) {
            start = true;
            step();
            start = false;
            std::vector<unsigned> sent;
            unsigned encoder_state = 0;
            constexpr unsigned length = 1024;
            for (unsigned index = 0; index < length + 32; ++index) {
                unsigned data = next_random(random) & 255;
                unsigned sd6 = (data >> 6) & 1, sd7 = data >> 7;
                unsigned subset = 4 * sd6 + 2 * sd7 + (encoder_state >> 2);
                encoder_state = (encoder_state >> 2) | ((sd6 ^ (encoder_state & 1)) << 1) |
                                ((sd7 ^ ((encoder_state >> 1) & 1)) << 2);
                std::array<int, 4> sample{};
                for (unsigned lane = 0; lane < 4; ++lane) {
                    int noise = int(next_random(random) % 25) - 12;
                    if (stream >= 4)
                        noise = int(next_random(random) % 25) + int(next_random(random) % 25) - 24;
                    if (stream >= 8)
                        noise = int(next_random(random) % 37) + int(next_random(random) % 37) - 36;
                    sample[lane] = 128 + 32 * constellation.symbols[subset][data & 63][lane] + noise;
                }
                constellation.nearest(sample, 0, metric, point);
                unsigned nearest = unsigned(std::min_element(metric.begin(), metric.end()) - metric.begin());
                unsigned hard = point[nearest] + ((nearest >> 2) << 6) + (((nearest >> 1) & 1) << 7);
                if (index < length && stream >= 4 && stream < 8)
                    hard_errors += hard != data;
                sent.push_back(data);
                // Pausing advances neither path metrics nor survivor memory.
                if (index % 29 == 0) {
                    advance = false;
                    step();
                }
                advance = true;
                unsigned decoded = step();
                if (index >= 32 && index - 32 < length) {
                    bool wrong = decoded != sent[index - 32];
                    if (stream < 4)
                        clean_errors += wrong;
                    else if (stream < 8)
                        soft_errors += wrong;
                    else
                        overload_errors += wrong;
                }
            }
        }
        if (clean_errors || !hard_errors || soft_errors >= hard_errors || !overload_errors)
            throw std::runtime_error(
                "trellis clean-stream/coding-gain contract failed: clean=" + std::to_string(clean_errors) +
                " soft=" + std::to_string(soft_errors) + " hard=" + std::to_string(hard_errors));
        start = true;
        step();
        // Explicit ending-state traceback must preserve a previous frame's tail
        // even when another stream starts before its 31-byte drain completes.
        std::deque<unsigned> expected;
        auto check_stream_output = [&](unsigned decoded) {
            if (observed_valid) {
                if (expected.empty() || decoded != expected.front())
                    throw std::runtime_error("terminated/back-to-back stream lost a byte");
                expected.pop_front();
            }
        };
        start = false;
        for (unsigned length : {72, 80, 3, 128}) {
            start = true;
            advance = false;
            check_stream_output(step());
            start = false;
            unsigned encoder_state = 0;
            for (unsigned index = 0; index < length + 2; ++index) {
                unsigned data = next_random(random) & 255;
                if (index >= length)
                    data = ((encoder_state & 1) << 6) | (((encoder_state >> 1) & 1) << 7);
                unsigned sd6 = (data >> 6) & 1, sd7 = data >> 7;
                unsigned subset = sd6 * 4 + sd7 * 2 + (encoder_state >> 2);
                encoder_state = (encoder_state >> 2) | ((sd6 ^ (encoder_state & 1)) << 1) |
                                ((sd7 ^ ((encoder_state >> 1) & 1)) << 2);
                std::array<int, 4> sample{};
                for (unsigned lane = 0; lane < 4; ++lane)
                    sample[lane] = 128 + 32 * constellation.symbols[subset][data & 63][lane];
                constellation.nearest(sample, 0, metric, point);
                expected.push_back(data);
                advance = true;
                end = index == length + 1;
                check_stream_output(step());
            }
            if (encoder_state != 0)
                throw std::runtime_error("reference termination did not reach state zero");
            advance = false;
            end = false;
            for (unsigned idle = 0; idle < 12; ++idle)
                check_stream_output(step());
        }
        for (unsigned idle = 0; idle < 32; ++idle)
            check_stream_output(step());
        if (!expected.empty())
            throw std::runtime_error("terminated stream failed to drain completely");
        // A large common metric offset changes no Viterbi decision. It forces
        // repeated circular wraps and checks full-range metrics and tie order
        // against the independent, unbounded absolute-cost dynamic program.
        start = true;
        step();
        start = false;
        advance = true;
        for (unsigned index = 0; index < 4096; ++index) {
            for (unsigned subset = 0; subset < 8; ++subset) {
                metric[subset] = index < 2048 ? 200000 + next_random(random) % 62144 : 262143;
                point[subset] = next_random(random) & 63;
            }
            step();
            if (index % 61 == 0) {
                advance = false;
                step();
                advance = true;
            }
        }
        std::cout << cycles << " trellis/full-path oracle steps; 4096 clean bytes exact; noisy byte errors "
                  << soft_errors << " versus " << hard_errors << " without trellis decoding\n";
        std::cout << "Excessive-noise negative control: " << overload_errors << " erroneous bytes detected\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
