#include <cstdint>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VSynchronousFrameFIFO.h"
using DUT = VSynchronousFrameFIFO;
#else
#include "../synchronous_frame_fifo.hpp"
using DUT = SynchronousFrameFIFO;
long _system_clock = -1;
#endif

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Bench {
    DUT dut;
    unsigned input = 0, output = 0, used = 0, faults = 0;
    bool valid = false, hold = false, fault = false, idle = false;
    std::uint64_t hash = 1469598103934665603ull, cycles = 0;
    Bench() {
#ifndef VERILATOR
        dut.receive_in = _ASSIGN(input); dut.valid_in = _ASSIGN(valid);
        dut.fault_in = _ASSIGN(fault); dut.hold_tx_in = _ASSIGN(hold);
        dut.clear_rx_in = _ASSIGN(false); dut.clear_tx_in = _ASSIGN(false);
        dut._assign();
#endif
        step(3, true);
    }
    void step(unsigned clocks, bool reset = false) {
#ifdef VERILATOR
        dut.rx_clk = dut.tx_clk = 0; dut.reset = reset;
        dut.receive_in = input; dut.valid_in = valid; dut.fault_in = fault;
        dut.hold_tx_in = hold; dut.clear_rx_in = dut.clear_tx_in = false;
        dut.eval(); dut.rx_clk = clocks & 1; dut.tx_clk = (clocks >> 1) & 1; dut.eval();
#define READ(name) unsigned(dut.name)
#else
        ++_system_clock;
        if (clocks & 1) dut._work_rx_clk(reset);
        if (clocks & 2) dut._work_tx_clk(reset);
        if (clocks & 1) dut._strobe_rx_clk();
        if (clocks & 2) dut._strobe_tx_clk();
        ++_system_clock;
#define READ(name) unsigned(dut.name())
#endif
        output = READ(transmit_out); used = READ(used_out); idle = READ(idle_out);
        faults = READ(source_fault_out) | (READ(fault_out) << 1);
#undef READ
        for (unsigned value : {clocks, unsigned(reset), input, unsigned(valid), unsigned(hold), unsigned(fault),
                               output, used, unsigned(idle), faults}) {
            hash ^= value; hash *= 1099511628211ull;
        }
        ++cycles;
    }
    void write(unsigned value) { input = value; valid = true; step(1); }
};

int main() {
    try {
        Bench bench;
        unsigned frames = 0, bytes = 0, fault_cases = 0;
        // Every batch starts with an empty read pipeline and the TX clock
        // stopped. Repetition crosses both SRAM bank and Gray-pointer wraps.
        for (unsigned batch = 0; batch < 606; ++batch) {
            unsigned length = batch < 6 ? std::initializer_list<unsigned>{1, 7, 16, 64, 1518, 1522}.begin()[batch] : 16;
            for (unsigned i = 0; i < length; ++i) bench.write(256 | ((i + batch) & 255));
            bench.write(0);
            bench.valid = false; bench.hold = batch % 2 != 0;
            for (unsigned i = 0; i < (bench.hold ? 30u : 0u); ++i) {
                bench.step(2); require(!bench.output && !bench.faults, "held FIFO started or faulted");
            }
            bench.hold = false;
            unsigned received = 0;
            for (unsigned i = 0; i < length + 40; ++i) {
                bench.step(2);
                require(!bench.faults, "prefilled SRAM falsely underflowed at TX restart");
                if (received > 0 && received < length)
                    require(bench.output & 256, "SRAM pipeline inserted an idle inside a frame");
                if (bench.output & 256) {
                    require(received < length && bench.output == (256 | ((received + batch) & 255)), "changed FIFO byte/order");
                    ++received;
                    // A new hold cannot truncate a frame already being sent.
                    if (received == 1) bench.hold = true;
                }
            }
            require(received == length && bench.idle, "lost frame or end marker across SRAM banks");
            bench.hold = false;
            bench.step(1); bench.step(1); bench.step(1);
            require(bench.used == 0, "read credit did not return to source");
            ++frames; bytes += received;
        }
        // A missing end marker is a real underrun, unlike initial read latency.
        for (unsigned kind = 0; kind < 3; ++kind) {
            bench.valid = false; bench.fault = false; bench.step(3, true);
            unsigned length = kind == 1 ? 4097 : 16;
            for (unsigned i = 0; i < length; ++i) bench.write(0x155);
            if (kind == 1) require(bench.faults & 1, "overflow was silently ignored");
            bench.valid = false;
            unsigned aborts = 0;
            for (unsigned i = 0; i < 80; ++i) {
                bench.fault = kind == 2 && i == 12;
                bench.step(3);
                if (bench.output == 768) ++aborts;
            }
            require((bench.faults & 2) && aborts == (kind == 1 ? 0u : 1u), "missing/repeated explicit abort");
            ++fault_cases;
        }
        bench.valid = false; bench.fault = false; bench.step(3, true);
        require(!bench.output && !bench.faults && !bench.used, "reset retained FIFO error/state");
        std::cout << "PASS " << frames << " prefilled frames " << bytes << " bytes " << fault_cases
                  << " fault cases " << bench.cycles << " cycles " << bench.hash << " digest\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
