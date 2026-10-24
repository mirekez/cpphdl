#ifdef VERILATOR
#include "VADCStartupSequencer.h"
#else
#include "../adc_startup.hpp"
#endif
#include <fstream>
#include <iostream>
#include <stdexcept>

#ifndef VERILATOR
long _system_clock = -1;
#define VALUE(name) dut.name()
#else
#define VALUE(name) dut.name
#endif

int main(int argc, char** argv) {
    try {
        if (argc > 2)
            throw std::runtime_error("expected optional startup trace path");
#ifdef VERILATOR
        VADCStartupSequencer dut;
#else
        ADCStartupSequencer<> dut;
#endif
        bool enabled = false;
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(enabled);
        dut._assign();
#endif
        std::ofstream trace;
        if (argc == 2) {
            trace.open(argv[1]);
            if (!trace)
                throw std::runtime_error("cannot open startup trace");
        }
        unsigned age = 0, ready_edges = 0, precharge_edges = 0, release_edges = 0;
        for (unsigned clock = 0; clock < 40000; ++clock) {
            // Interrupt once while tracking, once during opening, and once
            // after ready. Every interruption must restart the full sequence.
            const bool reset = clock < 4 || clock == 1200 || clock == 15296;
            enabled = clock >= 4 && clock != 6500 && clock != 10598 && !(21000 <= clock && clock < 21005);
            age = reset || !enabled ? 0 : age + 1;
#ifdef VERILATOR
            dut.enable_in = enabled;
            dut.reset = reset;
            dut.clk = 0;
            dut.eval();
            dut.clk = 1;
            dut.eval();
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
#endif
            const bool precharge = enabled && age < 4096;
            const bool ready = enabled && age >= 4100;
            const bool clear = !ready;
            if (bool(VALUE(precharge_out)) != precharge || bool(VALUE(ready_out)) != ready || bool(VALUE(clear_out)) != clear)
                throw std::runtime_error("ADC startup duration, reset or enable failure");
            precharge_edges += unsigned(precharge);
            ready_edges += unsigned(ready);
            release_edges += unsigned(enabled && !precharge && !ready);
            if (trace)
                trace << clock << ',' << enabled << ',' << reset << ',' << bool(VALUE(precharge_out)) << ',' << bool(VALUE(clear_out)) << ',' << bool(VALUE(ready_out)) << '\n';
        }
        if (!precharge_edges || !ready_edges || release_edges < 8)
            throw std::runtime_error("ADC startup coverage failure");
        if (argc == 2) {
            trace.flush();
            if (!trace)
                throw std::runtime_error("startup trace write failure");
        }
        std::cout << "PASS ADC startup: 40,000 clocks, complete precharge/open/ready and interrupted restart\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
