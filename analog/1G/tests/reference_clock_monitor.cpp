#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VReferenceClockMonitor.h"
using DUT = VReferenceClockMonitor;
#define READ(name) unsigned(dut.name)
#else
#include "../reference_clock_monitor.hpp"
using DUT = ReferenceClockMonitor;
long _system_clock = -1;
#define READ(name) unsigned(dut.name())
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected trace destination");
        std::ofstream trace(argv[1]);
        DUT dut;
        bool power = true;
        unsigned events = 0;
#ifndef VERILATOR
        dut.power_good_in = _ASSIGN(power);
        dut._assign();
#endif
        auto tick = [&](bool ref, bool adc, bool reset = false) {
#ifdef VERILATOR
            dut.ref_clk = 0; dut.adc_clk = 0;
            dut.reset = reset; dut.power_good_in = power;
            dut.eval();
            dut.ref_clk = ref; dut.adc_clk = adc;
            dut.eval();
#else
            ++_system_clock;
            if (ref) dut._work_ref_clk(reset);
            if (adc) dut._work_adc_clk(reset);
            if (ref) dut._strobe_ref_clk();
            if (adc) dut._strobe_adc_clk();
            ++_system_clock;
#endif
            trace << events++ << ',' << ref << ',' << adc << ',' << reset << ',' << power << ','
                  << READ(ref_ready_out) << ',' << READ(adc_ready_out) << ',' << READ(measured_edges_out) << '\n';
        };
        auto require = [](bool good, const char* message) {
            if (!good) throw std::runtime_error(message);
        };
        auto reset = [&]() { for (unsigned i = 0; i < 4; ++i) tick(true, true, true); };
        auto stable = [&]() {
            for (unsigned i = 0; i < 25000; ++i) {
                tick(true, true); tick(false, true);
                if (i < 8191)
                    require(!READ(ref_ready_out) && !READ(adc_ready_out), "qualified without two complete windows");
            }
            require(READ(ref_ready_out) && READ(adc_ready_out) && READ(measured_edges_out) == 8192,
                    "nominal clock failed qualification/wrap");
        };
        reset(); stable();
        // Supply withdrawal gates readiness even before the next clock edge.
        power = false;
        tick(false, false);
        require(!READ(ref_ready_out) && !READ(adc_ready_out), "power loss retained readiness");
        tick(true, true);
        power = true;
        reset(); stable();
        for (unsigned i = 0; i < 12; ++i) tick(true, false);
        require(!READ(ref_ready_out), "stopped ADC clock was not detected by reference domain");
        tick(false, true); tick(false, true);
        require(!READ(adc_ready_out), "ADC resumed with stale ready status");
        reset(); stable();
        for (unsigned i = 0; i < 24; ++i) tick(false, true);
        require(!READ(adc_ready_out), "stopped reference was not detected by ADC domain");
        for (unsigned i = 0; i < 100; ++i) { tick(true, true); tick(false, true); }
        require(!READ(adc_ready_out), "reference restarted without resetting loss latch");
        // Independent frequency patterns: +/- 1000 ppm fail; +/- 100 ppm
        // and phase-dithered 2:1 clocks qualify. Counters wrap repeatedly.
        for (unsigned profile = 0; profile < 5; ++profile) {
            reset();
            for (unsigned i = 0; i < 40000; ++i) {
                tick(true, true);
                const unsigned interval = profile < 2 ? 500 : 5000;
                const bool perturb = (i % interval) == interval - 1;
                if (!(perturb && (profile == 0 || profile == 2))) tick(false, true);
                if (perturb && (profile == 1 || profile == 3)) tick(false, true);
                if (profile == 4) tick(false, false);
                if (profile < 2)
                    require(!READ(ref_ready_out) && !READ(adc_ready_out), "wrong frequency qualified");
            }
            if (profile >= 2)
                require(READ(ref_ready_out) && READ(adc_ready_out), "in-band frequency failed qualification");
        }
        require(bool(trace), "trace write failed");
        std::cout << "PASS clock monitor: qualification, drift, wrap, stopped clocks, power loss and restart\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
