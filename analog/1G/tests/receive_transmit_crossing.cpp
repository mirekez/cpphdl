#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#include "VReceiveTransmitCrossing.h"
using DUT = VReceiveTransmitCrossing;
#else
#include "../receive_transmit_crossing.hpp"
using DUT = ReceiveTransmitCrossing;
long _system_clock = -1;
#endif

struct Bench {
    DUT dut;
    unsigned input = 0, output = 0;
    bool valid = false, fault = false, source_fault = false, destination_fault = false;
    std::ofstream& trace;
    unsigned& events;
    Bench(std::ofstream& stream, unsigned& count) : trace(stream), events(count) {
#ifndef VERILATOR
        dut.receive_in = _ASSIGN(input);
        dut.valid_in = _ASSIGN(valid);
        dut.fault_in = _ASSIGN(fault);
        dut.clear_rx_in = _ASSIGN(false);
        dut.clear_tx_in = _ASSIGN(false);
        dut.hold_tx_in = _ASSIGN(false);
        dut._assign();
#endif
        step(true, true, true);
    }
    void step(bool rx, bool tx, bool reset = false) {
#ifdef VERILATOR
        dut.rx_clk = 0; dut.tx_clk = 0;
        dut.reset = reset;
        dut.clear_rx_in = false; dut.clear_tx_in = false; dut.hold_tx_in = false;
        dut.receive_in = input; dut.valid_in = valid; dut.fault_in = fault;
        dut.eval();
        dut.rx_clk = rx; dut.tx_clk = tx;
        dut.eval();
        output = dut.transmit_out;
        source_fault = dut.source_fault_out;
        destination_fault = dut.fault_out;
#else
        ++_system_clock;
        if (rx) dut._work_rx_clk(reset);
        if (tx) dut._work_tx_clk(reset);
        if (rx) dut._strobe_rx_clk();
        if (tx) dut._strobe_tx_clk();
        ++_system_clock;
        output = unsigned(dut.transmit_out());
        source_fault = dut.source_fault_out();
        destination_fault = dut.fault_out();
#endif
        trace << events++ << ',' << rx << ',' << tx << ',' << reset << ',' << input << ','
              << valid << ',' << fault << ',' << output << ',' << source_fault << ',' << destination_fault << '\n';
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected event trace path");
        std::ofstream trace(argv[1]);
        if (!trace)
            throw std::runtime_error("cannot open crossing trace");
        unsigned events = 0, cases = 0, bytes = 0;
        for (unsigned source_period : {7999, 8000, 8001}) {
            for (unsigned transmit_period : {7999, 8000, 8001}) {
                for (unsigned phase : {0, 1, 3500, 7000}) {
                    Bench bench(trace, events);
                    std::vector<unsigned> stimulus(24, 0), expected;
                    std::vector<unsigned> boundaries;
                    for (unsigned length : {1, 7, 64, 1518, 9000, 64}) {
                        for (unsigned i = 0; i < length; ++i) {
                            unsigned word = 256 | ((i * 73 + length * 11 + cases) & 255);
                            if (i == length / 2)
                                word |= 512; // Preserve receive errors inside each frame.
                            stimulus.push_back(word);
                            expected.push_back(word);
                        }
                        boundaries.push_back(expected.size());
                        // Extra idle permits the same tests to include runt frames.
                        stimulus.insert(stimulus.end(), 24, 0);
                    }
                    stimulus.insert(stimulus.end(), 128, 0);
                    std::uint64_t next_rx = 0, next_tx = phase, next_symbol = 0;
                    unsigned supplied = 0, received = 0, frame = 0, idle = 100;
                    bool active = false;
                    while (supplied < stimulus.size()) {
                        auto time = std::min(next_rx, next_tx);
                        bool rx = time == next_rx, tx = time == next_tx;
                        bench.valid = rx && time >= next_symbol;
                        if (bench.valid) {
                            bench.input = stimulus[supplied++];
                            next_symbol += source_period;
                        }
                        bench.step(rx, tx);
                        if (rx) next_rx += 7000; // Faster processing clock, with symbol-enable bubbles.
                        if (tx) next_tx += transmit_period;
                        if (bench.source_fault || bench.destination_fault)
                            throw std::runtime_error("unexpected crossing fault in rate/phase case " + std::to_string(cases));
                        if (!tx)
                            continue;
                        if (bench.output & 256) {
                            if (!active && idle < 12)
                                throw std::runtime_error("shortened transmit interframe gap");
                            if (received >= expected.size() || bench.output != expected[received++])
                                throw std::runtime_error("changed, duplicated or reordered frame byte");
                            active = true;
                            idle = 0;
                        } else {
                            if (active) {
                                if (frame >= boundaries.size() || received != boundaries[frame++])
                                    throw std::runtime_error("broken or merged frame boundary");
                            }
                            active = false;
                            ++idle;
                        }
                    }
                    if (received != expected.size() || frame != boundaries.size())
                        throw std::runtime_error("lost buffered frames");
                    bytes += received;
                    ++cases;
                }
            }
        }
        // No destination edges: fill all 64 slots, then require overflow to
        // propagate through the actual destination synchronizers.
        {
            Bench bench(trace, events);
            bench.valid = true;
            for (unsigned i = 0; i < 65; ++i) {
                bench.input = 256 | i;
                bench.step(true, false);
                if (bench.source_fault != (i == 64))
                    throw std::runtime_error("incorrect full/overflow boundary");
            }
            bench.valid = false;
            for (unsigned i = 0; i < 5; ++i)
                bench.step(false, true);
            if (!bench.destination_fault)
                throw std::runtime_error("overflow was not delivered to the transmitter");
            bench.step(true, true, true);
            if (bench.source_fault || bench.destination_fault || bench.output)
                throw std::runtime_error("joint reset failed to clear crossing fault");
        }
        // Stop the source halfway through a frame, without an end marker.
        // Require an explicit error symbol and a sticky destination fault.
        {
            Bench bench(trace, events);
            bench.valid = true;
            for (unsigned i = 0; i < 16; ++i) {
                bench.input = 256 | i;
                bench.step(true, false);
            }
            bench.valid = false;
            unsigned delivered = 0, errors = 0;
            for (unsigned i = 0; i < 40; ++i) {
                bench.step(false, true);
                if (bench.output == 768)
                    ++errors;
                else if (bench.output & 256) {
                    if (bench.output != (256 | delivered++))
                        throw std::runtime_error("changed prefix before underrun");
                }
            }
            if (delivered != 16 || errors != 1 || !bench.destination_fault)
                throw std::runtime_error("underrun silently truncated a frame");
        }
        // An upstream receive fault aborts an active frame exactly once.
        {
            Bench bench(trace, events);
            bench.valid = true;
            for (unsigned i = 0; i < 20; ++i) {
                bench.input = 256 | i;
                bench.step(true, false);
            }
            for (unsigned i = 0; i < 5; ++i)
                bench.step(false, true);
            bench.valid = false;
            bench.fault = true;
            bench.step(true, false);
            unsigned errors = 0;
            for (unsigned i = 0; i < 12; ++i) {
                bench.step(false, true);
                errors += bench.output == 768;
            }
            if (errors != 1 || !bench.destination_fault || !bench.source_fault)
                throw std::runtime_error("receive failure did not abort the transmitted frame");
        }
        if (!trace)
            throw std::runtime_error("incomplete crossing trace");
        std::cout << "PASS " << cases << " rate/phase cases, " << bytes << " frame bytes, " << events
                  << " events; frame boundaries, errors, IFG, overflow, underrun and reset\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
