#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#include "VRepeaterForwarding.h"
using DUT = VRepeaterForwarding;
#else
#include "../repeater_forwarding.hpp"
using DUT = RepeaterForwarding;
long _system_clock = -1;
#endif

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct Bench {
    DUT dut;
    unsigned a = 0, b = 0, ready = 0, faults = 0, holds = 15;
    unsigned out_a = 0, out_b = 0;
    unsigned pause_a = 0, pause_b = 0;
    bool hold_a = false, hold_b = false;
    bool enable = false, valid_a = false, valid_b = false, fault_a = false, fault_b = false;
    std::ofstream& trace;
    unsigned& events;

    Bench(std::ofstream& stream, unsigned& count) : trace(stream), events(count) {
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(enable);
        dut.hold_a_in = _ASSIGN(hold_a); dut.hold_b_in = _ASSIGN(hold_b);
        dut.rx_a_in = _ASSIGN(a);
        dut.rx_b_in = _ASSIGN(b);
        dut.valid_a_in = _ASSIGN(valid_a);
        dut.valid_b_in = _ASSIGN(valid_b);
        dut.fault_a_in = _ASSIGN(fault_a);
        dut.fault_b_in = _ASSIGN(fault_b);
        dut.rx_a_ready_in = _ASSIGN((ready & 1) != 0);
        dut.rx_b_ready_in = _ASSIGN((ready & 2) != 0);
        dut.tx_a_ready_in = _ASSIGN((ready & 4) != 0);
        dut.tx_b_ready_in = _ASSIGN((ready & 8) != 0);
        dut._assign();
#endif
        step(0, true);
    }

    void step(unsigned clocks, bool reset = false) {
#ifdef VERILATOR
        dut.rx_clk = dut.b_rx_clk = dut.a_tx_clk = dut.tx_clk = 0;
        dut.reset = reset;
        dut.enable_in = enable;
        dut.hold_a_in = hold_a; dut.hold_b_in = hold_b;
        dut.rx_a_in = a; dut.rx_b_in = b;
        dut.valid_a_in = valid_a; dut.valid_b_in = valid_b;
        dut.fault_a_in = fault_a; dut.fault_b_in = fault_b;
        dut.rx_a_ready_in = ready & 1; dut.rx_b_ready_in = (ready >> 1) & 1;
        dut.tx_a_ready_in = (ready >> 2) & 1; dut.tx_b_ready_in = (ready >> 3) & 1;
        dut.eval();
        dut.rx_clk = clocks & 1; dut.b_rx_clk = (clocks >> 1) & 1;
        dut.a_tx_clk = (clocks >> 2) & 1; dut.tx_clk = (clocks >> 3) & 1;
        dut.eval();
#define READ(name) unsigned(dut.name)
#else
        bool clear_ab = reset || !enable || (ready & 9) != 9;
        bool clear_ba = reset || !enable || (ready & 6) != 6;
        ++_system_clock;
        if (clocks & 1) dut._work_rx_clk(reset);
        if (clocks & 2) dut._work_b_rx_clk(reset);
        if (clocks & 4) dut._work_a_tx_clk(reset);
        if (clocks & 8) dut._work_tx_clk(reset);
        if (clear_ab) dut.sim_assert_a_to_b_reset();
        if (clear_ba) dut.sim_assert_b_to_a_reset();
        if ((clocks & 1) || clear_ab) dut._strobe_rx_clk();
        if ((clocks & 2) || clear_ba) dut._strobe_b_rx_clk();
        if ((clocks & 4) || clear_ba) dut._strobe_a_tx_clk();
        if ((clocks & 8) || clear_ab) dut._strobe_tx_clk();
        ++_system_clock;
#define READ(name) unsigned(dut.name())
#endif
        out_a = READ(tx_a_out); out_b = READ(tx_b_out);
        pause_a = READ(pause_a_out); pause_b = READ(pause_b_out);
        holds = READ(reset_a_rx_out) | (READ(reset_b_rx_out) << 1) |
                (READ(reset_a_tx_out) << 2) | (READ(reset_b_tx_out) << 3);
        faults = READ(source_fault_a_out) | (READ(source_fault_b_out) << 1) |
                 (READ(tx_fault_a_out) << 2) | (READ(tx_fault_b_out) << 3);
#undef READ
        trace << events++ << ',' << clocks << ',' << reset << ',' << enable << ',' << ready << ','
              << a << ',' << b << ',' << valid_a << ',' << valid_b << ',' << fault_a << ',' << fault_b << ','
              << out_a << ',' << out_b << ',' << holds << ',' << faults << ',' << pause_a << ',' << pause_b << '\n';
    }

    void start() {
        enable = true; ready = 15;
        for (unsigned i = 0; i < 6; ++i)
            step(15);
        require(holds == 0 && faults == 0, "forwarding failed joint reset release");
    }
};

struct Stream {
    std::vector<unsigned> stimulus, expected, boundaries;
    unsigned received = 0, frames = 0, idle = 100;
    bool active = false;

    explicit Stream(unsigned salt) {
        stimulus.resize(24);
        for (unsigned length : {1, 7, 64, 1518, 9000, 64}) {
            for (unsigned i = 0; i < length; ++i) {
                unsigned word = 256 | ((i * 73 + length * 11 + salt) & 255);
                if (i == length / 2)
                    word |= 512;
                stimulus.push_back(word);
                expected.push_back(word);
            }
            boundaries.push_back(expected.size());
            stimulus.insert(stimulus.end(), 24, 0);
        }
        stimulus.insert(stimulus.end(), 256, 0);
    }

    void observe(unsigned word) {
        if (word & 256) {
            require(active || idle >= 12, "short transmit interframe gap");
            require(received < expected.size() && word == expected[received], "changed/reordered/dropped frame byte");
            ++received;
            active = true;
            idle = 0;
        } else {
            if (active) {
                require(frames < boundaries.size() && received == boundaries[frames], "changed frame boundary");
                ++frames;
            }
            active = false;
            ++idle;
        }
    }
};

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected full event trace path");
        std::ofstream trace(argv[1]);
        trace.exceptions(std::ios::failbit | std::ios::badbit);
        unsigned events = 0, bytes = 0, cases = 0, reset_cases = 0, fault_cases = 0;
        for (unsigned phase : {0, 1, 3500, 7000}) {
            for (unsigned variation : {0, 1, 2}) {
                Bench bench(trace, events);
                bench.start();
                Stream from_a(phase + variation), from_b(phase + variation + 19);
                std::array<std::uint64_t, 4> next{0, phase, phase / 2, phase + 9};
                std::array<unsigned, 4> periods{7353 + variation * 110, 7575 - variation * 100,
                                               7999 + variation, 8001 - variation};
                std::uint64_t symbol_a = 0, symbol_b = phase;
                unsigned supplied_a = 0, supplied_b = 0;
                while (supplied_a < from_a.stimulus.size() || supplied_b < from_b.stimulus.size()) {
                    auto time = *std::min_element(next.begin(), next.end());
                    unsigned clocks = 0;
                    for (unsigned i = 0; i < 4; ++i)
                        if (time == next[i]) { clocks |= 1u << i; next[i] += periods[i]; }
                    bench.valid_a = (clocks & 1) && time >= symbol_a && supplied_a < from_a.stimulus.size();
                    bench.valid_b = (clocks & 2) && time >= symbol_b && supplied_b < from_b.stimulus.size();
                    if (bench.valid_a) { bench.a = from_a.stimulus[supplied_a++]; symbol_a += 7999 + variation; }
                    if (bench.valid_b) { bench.b = from_b.stimulus[supplied_b++]; symbol_b += 8001 - variation; }
                    bench.step(clocks);
                    require(bench.holds == 0 && bench.faults == 0, "unexpected forwarding reset/fault");
                    require(bench.pause_a == 0 && bench.pause_b == 0, "unnecessary pause in isolated frame bursts");
                    if (clocks & 4) from_b.observe(bench.out_a);
                    if (clocks & 8) from_a.observe(bench.out_b);
                }
                require(from_a.received == from_a.expected.size() && from_b.received == from_b.expected.size(), "lost forwarded bytes");
                require(from_a.frames == 6 && from_b.frames == 6, "lost forwarded frame ends");
                bytes += from_a.received + from_b.received;
                ++cases;
            }
        }
        // Withdraw every readiness input with all clocks stopped. Restart the
        // domains in either order; neither may reuse the peer's old FIFO state.
        for (unsigned withdrawn : {1, 2, 4, 8}) {
            for (unsigned order : {0, 1}) {
                Bench bench(trace, events);
                bench.start();
                unsigned pair = (withdrawn & 9) ? 9 : 6;
                unsigned first = pair == 9 ? (order ? 8 : 1) : (order ? 4 : 2);
                unsigned second = pair ^ first;
                bench.a = 0x155; bench.b = 0x1aa; bench.valid_a = bench.valid_b = true;
                for (unsigned i = 0; i < 20; ++i) bench.step(15);
                bench.ready &= ~withdrawn;
                bench.step(0);
                require((bench.holds & pair) == pair && (bench.holds & (15 ^ pair)) == 0,
                        "clock withdrawal failed asynchronous/independent assertion");
                require((pair == 9 ? bench.out_b : bench.out_a) == 0, "held domain leaked old frame output");
                bench.valid_a = bench.valid_b = false;
                bench.ready = 15;
                for (unsigned i = 0; i < 10; ++i) {
                    bench.step(first);
                    require((bench.holds & pair) == pair, "FIFO released while its peer clock was stopped");
                }
                for (unsigned i = 0; i < 10; ++i) bench.step(second);
                // The peer can now acknowledge, but this domain has not sampled it.
                require((bench.holds & first) != 0, "unclocked domain released without peer acknowledgement");
                for (unsigned i = 0; i < 8; ++i) bench.step(pair);
                require((bench.holds & pair) == 0, "joint reset did not complete");
                require((pair == 9 ? bench.out_b : bench.out_a) == 0, "stale frame survived coordinated restart");
                ++reset_cases;
            }
        }
        for (unsigned direction : {0, 1}) {
            Bench bench(trace, events);
            bench.start();
            bench.a = 0x155; bench.b = 0x1aa; bench.valid_a = bench.valid_b = true;
            unsigned intact_bytes = 0, aborts = 0;
            for (unsigned i = 0; i < 60; ++i) {
                bench.fault_a = i == 20 && direction == 0;
                bench.fault_b = i == 20 && direction == 1;
                if (i == 20) bench.a = bench.b = 0;
                bench.step(15);
                unsigned intact = direction == 0 ? bench.out_a : bench.out_b;
                unsigned failed = direction == 0 ? bench.out_b : bench.out_a;
                if (intact & 256) {
                    require(intact == (direction == 0 ? 0x1aa : 0x155), "fault crossed into the opposite frame");
                    ++intact_bytes;
                }
                if (failed == 768) ++aborts;
            }
            require(intact_bytes == 20 && aborts == 1, "lost intact direction or missing/duplicate frame abort");
            require(bench.faults == (direction == 0 ? 9u : 6u), "fault did not remain isolated and sticky");
            bench.enable = false;
            bench.step(0);
            require(bench.holds == 15 && bench.out_a == 0 && bench.out_b == 0, "disable failed while clocks stopped");
            bench.valid_a = bench.valid_b = false;
            bench.start();
            // Write fresh data over the old ring entries after joint release.
            unsigned delivered_a = 0, delivered_b = 0;
            for (unsigned i = 0; i < 60; ++i) {
                bench.valid_a = bench.valid_b = true;
                bench.a = i < 16 ? 0x133 : 0;
                bench.b = i < 16 ? 0x1cc : 0;
                bench.step(15);
                require(bench.faults == 0, "fault survived coordinated restart");
                if (bench.out_a & 256) { require(bench.out_a == 0x1cc, "stale B data after restart"); ++delivered_a; }
                if (bench.out_b & 256) { require(bench.out_b == 0x133, "stale A data after restart"); ++delivered_b; }
            }
            require(delivered_a == 16 && delivered_b == 16, "lost fresh frames after fault recovery");
            ++fault_cases;
        }
        unsigned pressure_frames = 0, paused_symbols = 0, pause_requests = 0;
        {
            Bench bench(trace, events);
            bench.start();
            std::uint64_t next_rx = 0, next_tx = 31111, next_symbol = 0;
            unsigned supplied = 0, offset = 0, frame_id = 0, previous_id = 0, idle = 100;
            bool active = false, have_previous = false;
            const unsigned frame_count = 512, length = 1518, span = length + 12;
            std::array<unsigned, 1800> pause_delay{};
            unsigned symbol_count = 0;
            bool previous_request = false;
            while (supplied < frame_count * span + 4096) {
                auto time = std::min(next_rx, next_tx);
                bool rx = time == next_rx, tx = time == next_tx;
                if (rx) next_rx += 74630;
                if (tx) next_tx += 80008; // -100 ppm transmitter; units are 0.1 ps.
                bench.valid_a = rx && time >= next_symbol;
                if (bench.valid_a) {
                    unsigned id = supplied / span, position = supplied % span;
                    unsigned data = position == 0 ? id & 255 : position == 1 ? id >> 8 : (position * 73 + id * 19) & 255;
                    bool paused = pause_delay[symbol_count % pause_delay.size()] != 0;
                    pause_delay[symbol_count % pause_delay.size()] = bench.pause_a;
                    ++symbol_count;
                    // Model a finite peer response and finish its active frame.
                    // Protocol wire encoding is checked by the PAUSE MAC tests.
                    if (paused && position == 0 && id < frame_count) {
                        bench.a = 0;
                        ++paused_symbols;
                    } else {
                        bench.a = id < frame_count && position < length ? 256 | data | (position == length / 2 ? 512 : 0) : 0;
                        ++supplied;
                    }
                    bench.hold_b = symbol_count < 3500;
                    next_symbol += 79992; // +100 ppm recovered source, minimum legal IFG.
                }
                bench.step(unsigned(rx) | (unsigned(tx) << 3));
                require(bench.faults == 0 && bench.holds == 0, "sustained rate mismatch broke forwarding");
                if (bench.pause_a && !previous_request) ++pause_requests;
                previous_request = bench.pause_a;
                if (!tx) continue;
                if (bench.out_b & 256) {
                    if (!active) {
                        require(idle >= 12, "pressure case shortened IFG");
                        offset = 0;
                        active = true;
                    }
                    if (offset == 0) frame_id = bench.out_b & 255;
                    else if (offset == 1) {
                        frame_id |= (bench.out_b & 255) << 8;
                        require(frame_id < frame_count && (!have_previous || frame_id == previous_id + 1), "reordered pressure frames");
                    }
                    unsigned data = offset == 0 ? frame_id & 255 : offset == 1 ? frame_id >> 8 : (offset * 73 + frame_id * 19) & 255;
                    require(offset < length && bench.out_b == (256 | data | (offset == length / 2 ? 512 : 0)),
                            "partial/corrupted pressure frame");
                    ++offset;
                    idle = 0;
                } else {
                    if (active) {
                        require(offset == length, "pressure discarded part of a frame");
                        ++pressure_frames;
                        previous_id = frame_id;
                        have_previous = true;
                    }
                    active = false;
                    ++idle;
                }
            }
            require(pressure_frames == frame_count && paused_symbols > 0 && pause_requests > 0 && bench.pause_b == 0,
                    "flow control lost frames or failed to pause/resume its source");
        }
        trace.close();
        std::cout << "PASS " << cases << " bidirectional clock cases, " << bytes << " bytes, " << reset_cases
                  << " stopped-clock reset cases, " << fault_cases << " fault/recovery cases, " << events << " exact events\n";
        std::cout << "PRESSURE " << pressure_frames << " complete frames, " << paused_symbols << " paused symbols, " << pause_requests << " requests\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
