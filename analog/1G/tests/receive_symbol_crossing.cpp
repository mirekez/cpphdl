#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VReceiveSymbolCrossing.h"
using DUT = VReceiveSymbolCrossing;
#else
#include "../receive_symbol_crossing.hpp"
using DUT = ReceiveSymbolCrossing;
long _system_clock = -1;
#endif

namespace {
std::ofstream trace;
unsigned events = 0, checked = 0;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Word {
    unsigned samples, tag;
    bool clipped;
};
class Bench {
public:
    DUT dut;
    unsigned samples = 0, tag = 0;
    bool clipped = false, valid = false, fault = false;
    bool output_valid = false, output_fault = false, source_fault = false;
    Word output{};
    Bench() {
#ifndef VERILATOR
        dut.samples_in = _ASSIGN(samples);
        dut.tag_in = _ASSIGN(tag);
        dut.clipped_in = _ASSIGN(clipped);
        dut.valid_in = _ASSIGN(valid);
        dut.fault_in = _ASSIGN(fault);
        dut._assign();
#endif
    }
    void tick(bool adc, bool pcs, bool reset = false) {
#ifdef VERILATOR
        dut.reset = reset;
        dut.adc_clk = 0;
        dut.pcs_clk = 0;
        dut.samples_in = samples;
        dut.tag_in = tag;
        dut.clipped_in = clipped;
        dut.valid_in = valid;
        dut.fault_in = fault;
        dut.eval();
        dut.adc_clk = adc;
        dut.pcs_clk = pcs;
        dut.eval();
        output_valid = dut.valid_out;
        output_fault = dut.fault_out;
        source_fault = dut.source_fault_out;
        output = {unsigned(dut.samples_out), unsigned(dut.tag_out), bool(dut.clipped_out)};
#else
        ++_system_clock;
        // Simultaneous clock edges see the same old-state snapshot.
        if (adc)
            dut._work_adc_clk(reset);
        if (pcs)
            dut._work_pcs_clk(reset);
        if (adc)
            dut._strobe_adc_clk();
        if (pcs)
            dut._strobe_pcs_clk();
        output_valid = dut.valid_out();
        output_fault = dut.fault_out();
        source_fault = dut.source_fault_out();
        output = {unsigned(dut.samples_out()), unsigned(dut.tag_out()), bool(dut.clipped_out())};
#endif
        if (trace)
            trace << events << ',' << adc << ',' << pcs << ',' << reset << ',' << output_valid << ','
                  << output.samples << ',' << output.tag << ',' << output.clipped << ','
                  << source_fault << ',' << output_fault << '\n';
        ++events;
    }
    void reset() {
        // Reset wins over valid data and incoming faults in both domains.
        valid = true;
        fault = true;
        for (unsigned i = 0; i < 4; ++i) {
            tick(true, false, true);
            tick(false, true, true);
        }
        valid = false;
        fault = false;
        require(!source_fault && !output_fault && !output_valid, "joint reset failed");
    }
};

void check_word(const Word& actual, const Word& expected) {
    require(actual.samples == expected.samples && actual.tag == expected.tag &&
            actual.clipped == expected.clipped, "symbol bundle was lost, reordered or corrupted");
    ++checked;
}

void rate_case(unsigned phase, unsigned rate) {
    Bench bench;
    bench.reset();
    std::deque<Word> expected;
    unsigned accumulator = 0, produced = 0, producer_clocks = 0;
    unsigned random = 0x1681aceu + phase;
    for (unsigned time = 0; time < 18000; ++time) {
        // Half-nanosecond ticks: 4 ns ADC and 7 ns candidate PCS clocks.
        const bool adc = time % 8 == 0;
        const bool pcs = time % 14 == phase;
        if (!adc && !pcs)
            continue;
        if (adc) {
            bench.valid = false;
            if (producer_clocks++ < 2000) {
                accumulator += rate;
                bench.valid = accumulator >= 1000000;
                if (bench.valid) {
                    accumulator -= 1000000;
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    bench.samples = random;
                    bench.tag = (65500 + produced++) & 65535;
                    bench.clipped = produced % 7 == 0;
                    expected.push_back({bench.samples, bench.tag, bench.clipped});
                }
            }
        }
        bench.tick(adc, pcs);
        require(!bench.source_fault && !bench.output_fault, "FIFO overflow within the declared rate envelope");
        if (pcs && bench.output_valid) {
            require(!expected.empty(), "spurious or duplicated symbol tick");
            check_word(bench.output, expected.front());
            expected.pop_front();
        }
    }
    require(expected.empty(), "symbols remained after the drain interval");
}

void fault_cases() {
    for (bool external_fault : {false, true}) {
        Bench bench;
        bench.reset();
        // A stopped PCS clock must neither overwrite full storage nor hide loss.
        for (unsigned i = 0; i < 9; ++i) {
            bench.valid = true;
            bench.samples = 0xa5000000u + i;
            bench.tag = i;
            bench.fault = external_fault && i == 3;
            bench.tick(true, false);
        }
        require(bench.source_fault, "missing sticky source fault");
        bench.valid = false;
        bench.fault = false;
        unsigned delivered = 0;
        for (unsigned i = 0; i < 20; ++i) {
            bench.tick(true, true);
            if (bench.output_valid) {
                check_word(bench.output, {0xa5000000u + delivered, delivered, false});
                ++delivered;
            }
            if (i >= 2)
                require(bench.output_fault && !bench.output_valid, "fault crossing did not stop output");
        }
        require(delivered <= 2, "fault propagated too late");
        bench.reset();
        bench.valid = true;
        bench.samples = 0x12345678;
        bench.tag = 0xbabe;
        bench.clipped = true;
        bench.tick(true, false);
        bench.valid = false;
        bool received = false;
        for (unsigned i = 0; i < 10; ++i) {
            bench.tick(true, true);
            if (bench.output_valid) {
                require(!received, "duplicate symbol after reset");
                check_word(bench.output, {0x12345678, 0xbabe, true});
                received = true;
            }
        }
        require(received && !bench.output_fault, "joint reset did not restore clean transfer");
    }
    Bench live;
    live.reset();
    for (unsigned i = 0; i < 6; ++i) {
        live.samples = 0x5a000000u + i;
        live.tag = 100 + i;
        live.valid = true;
        live.tick(true, false);
    }
    live.valid = false;
    live.tick(false, true);
    live.tick(false, true);
    // The simultaneous source fault and read edge share old state. Up to
    // three already-published words precede the synchronized sticky fault.
    unsigned delivered = 0;
    for (unsigned i = 0; i < 12; ++i) {
        live.fault = i == 0;
        live.tick(true, true);
        if (live.output_valid) {
            check_word(live.output, {0x5a000000u + delivered, 100 + delivered, false});
            ++delivered;
        }
        if (i >= 3)
            require(live.output_fault && !live.output_valid, "in-flight fault did not stop delivery");
    }
    require(delivered == 3, "unexpected fault synchronization latency");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            trace.open(argv[1]);
            require(bool(trace), "cannot open trace");
        }
        // 0.522 includes the resampler's maximum acquisition actuator rate,
        // beyond the +/-1500 ppm remote frequency cases around 0.5.
        for (unsigned rate : {480000u, 499250u, 500000u, 500750u, 522000u})
            for (unsigned phase = 0; phase < 14; ++phase)
                rate_case(phase, rate);
        fault_cases();
        std::cout << "70 clock-phase/rate cases, " << checked << " exact symbol bundles, "
                  << events << " events; tag wrap, clipping, pause, overflow, fault and reset passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
