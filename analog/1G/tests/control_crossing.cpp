#ifdef VERILATOR
#include "VControlCrossing.h"
using DUT = VControlCrossing;
#define READ(name) unsigned(dut.name)
#else
#include "../control_crossing.hpp"
long _system_clock = -1;
using DUT = ControlCrossing<32>;
#define READ(name) unsigned(dut.name())
#endif
#include <cstdint>
#include <iostream>
#include <stdexcept>

int main() {
    DUT dut;
    unsigned input = 0, sequence = 0, previous = 0, deliveries = 0;
    std::uint64_t digest = 1469598103934665603ULL;
#ifndef VERILATOR
    dut.data_in = _ASSIGN(input);
    dut._assign();
#else
    dut.source_clk = dut.destination_clk = dut.reset = 0;
    dut.data_in = 0; dut.eval();
#endif
    auto tick = [&](bool source, bool destination, bool reset) {
#ifdef VERILATOR
        dut.source_clk = dut.destination_clk = 0;
        dut.reset = reset; dut.data_in = input; dut.eval();
        dut.source_clk = source; dut.destination_clk = destination; dut.eval();
#else
        ++_system_clock;
        if (reset) {
            dut._reset_pos_source_clk(); dut._reset_pos_destination_clk();
        } else {
            if (source) dut._work_source_clk(false);
            if (destination) dut._work_destination_clk(false);
        }
        if (source || reset) dut._strobe_source_clk();
        if (destination || reset) dut._strobe_destination_clk();
        ++_system_clock;
#endif
        if (reset && (READ(valid_out) || READ(data_out)))
            throw std::runtime_error("stopped-clock reset retained a command");
        if (READ(valid_out)) {
            const unsigned word = READ(data_out), serial = word & 65535;
            if ((word >> 16) != (serial ^ 65535) || serial > sequence || serial < previous)
                throw std::runtime_error("torn, reordered or invented control word");
            if (serial != previous) ++deliveries;
            previous = serial;
        }
        digest = (digest ^ READ(data_out)) * 1099511628211ULL;
        digest = (digest ^ READ(valid_out)) * 1099511628211ULL;
    };
    tick(false, false, true);
    for (unsigned t = 0; t < 20000; ++t) {
        const bool source = t % 3 == 0 && !(t >= 5000 && t < 6000);
        const bool destination = t % 7 == 0 && !(t >= 10000 && t < 11000);
        const bool reset = t >= 8000 && t < 8030;
        if (reset) { sequence = previous = 0; }
        if (source) ++sequence;
        input = sequence | ((sequence ^ 65535) << 16);
        tick(source, destination, reset);
    }
    for (unsigned t = 0; t < 200; ++t) tick(t % 3 == 0, t % 7 == 0, false);
    if (!READ(valid_out) || READ(data_out) != input || deliveries < 200)
        throw std::runtime_error("latest control word failed to arrive");
    tick(false, false, true);
    std::cout << "PASS coherent control crossing: " << deliveries << " updates; " << digest << " digest\n";
}
