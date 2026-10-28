#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VProcessingClockControl.h"
using DUT = VProcessingClockControl;
#define READ(name) unsigned(dut.name)
#else
#include "../processing_clock_control.hpp"
using DUT = ProcessingClockControl;
long _system_clock = -1;
#define READ(name) unsigned(dut.name())
#endif

#include "processing_clock_load_step.hpp"
#include "processing_clock_drift.hpp"

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected trace destination");
        std::ofstream trace(argv[1]);
        auto require = [](bool good, const char* message) {
            if (!good) throw std::runtime_error(message);
        };
        unsigned qualified = 0, rejected = 0, events = 0;
        for (unsigned profile = 0; profile < 8; ++profile) {
            DUT dut;
            bool enable = true;
            unsigned reference_since_ready = 100000, previous_trim = 0;
            bool first_tick = true;
#ifndef VERILATOR
            dut.enable_in = _ASSIGN(enable);
            dut._assign();
#else
            // Establish low reset before testing its asynchronous assertion.
            dut.ref_clk = 0;
            dut.pcs_clk = 0;
            dut.reset = 0;
            dut.enable_in = enable;
            dut.eval();
#endif
            auto tick = [&](bool ref, bool pcs, bool reset = false) {
#ifdef VERILATOR
                dut.ref_clk = 0; dut.pcs_clk = 0; dut.reset = reset; dut.enable_in = enable;
                dut.eval();
                dut.ref_clk = ref; dut.pcs_clk = pcs; dut.eval();
#else
                ++_system_clock;
                if (reset) {
                    dut._reset_pos_ref_clk();
                    dut._reset_pos_pcs_clk();
                } else {
                    if (ref) dut._work_ref_clk(false);
                    if (pcs) dut._work_pcs_clk(false);
                }
                if (ref || reset) dut._strobe_ref_clk();
                if (pcs || reset) dut._strobe_pcs_clk();
                ++_system_clock;
#endif
                const unsigned trim = ((READ(coarse_n_out) ^ 255) << 8) | (READ(fine_n_out) ^ 255);
                if (READ(ready_ref_out) || READ(ready_pcs_out))
                    reference_since_ready = 0;
                else if (ref && reference_since_ready < 100000)
                    ++reference_since_ready;
                if (!first_tick && !reset && trim != previous_trim)
                    require(reference_since_ready >= 32, "DAC changed before readiness withdrawal crossed the domains");
                if (reset)
                    reference_since_ready = 100000;
                previous_trim = trim;
                first_tick = false;
                trace << events++ << ',' << profile << ',' << ref << ',' << pcs << ',' << reset << ','
                      << READ(coarse_n_out) << ',' << READ(fine_n_out) << ',' << READ(clear_out) << ','
                      << READ(ready_ref_out) << ',' << READ(ready_pcs_out) << ',' << READ(fault_out) << ','
                      << READ(measured_edges_out) << '\n';
            };
            tick(false, false, true);
            require(READ(clear_out) && !READ(ready_ref_out) && !READ(ready_pcs_out) &&
                    READ(coarse_n_out) == 127 && READ(fine_n_out) == 255,
                    "reset needs no reference or oscillator edge");
            for (unsigned i = 0; i < 4; ++i) tick(true, true, true);
            double phase = 0, frequency = 0;
            bool was_ready = false;
            // Keep one locked run long enough to wrap the 14-bit raw-clock
            // counter twice. Readiness must survive modular measurements.
            for (unsigned ns = 0; ns < (profile == 0 ? 300000u : 100000u); ++ns) {
                const unsigned coarse = READ(coarse_n_out) ^ 255, fine = READ(fine_n_out) ^ 255;
                const double code = (255.0 * coarse + fine) / 65280;
                // Independent monotonic actuator examples include a dead
                // zone and untunable fast/slow corners. This tests the digital
                // search; process transistor tests establish the actual curve.
                frequency = profile == 0 ? 40 + 200 * code : profile == 1 ? 20 + 160 * code :
                            profile == 2 ? 80 + 320 * code : profile == 3 ? (code < .3 ? 0 : (code - .3) * 360) :
                            profile == 4 ? 160 + 200 * code : profile == 5 ? 100 * code :
                            profile == 6 ? 20 + 760 * code : 680 + 100 * code;
                if (READ(clear_out)) phase = 0;
                else phase += frequency / 1000;
                const bool pcs = phase >= 1;
                if (pcs) phase -= 1;
                const bool ref = ns % 8 == 0;
                if (ref || pcs) tick(ref, pcs);
                if (READ(ready_ref_out) || READ(ready_pcs_out)) {
                    require((profile < 4 || profile == 6) && frequency > 132 && frequency < 136, "qualified an out-of-band oscillator");
                    was_ready = true;
                }
                if ((profile < 4 || profile == 6)) require(!READ(fault_out), "tunable actuator search failed");
            }
            if ((profile < 4 || profile == 6)) {
                require(was_ready && READ(ready_ref_out) && READ(ready_pcs_out), "missing calibrated clock readiness");
                ++qualified;
                if (profile == 0) {
                    // A changed measurement is visible before the following
                    // reference edge makes its decision. Stop exactly there:
                    // the pending decision must not disarm the raw watchdog.
                    const unsigned previous = READ(measured_edges_out);
                    bool boundary = false;
                    for (unsigned ns = 300000; ns < 310000 && !boundary; ++ns) {
                        phase += frequency / 1000;
                        const bool pcs = phase >= 1;
                        if (pcs) phase -= 1;
                        const bool ref = ns % 8 == 0;
                        if (ref || pcs) tick(ref, pcs);
                        boundary = READ(measured_edges_out) != previous;
                    }
                    require(boundary && READ(ready_pcs_out), "missing qualified measurement boundary");
                }
                for (unsigned i = 0; i < 24; ++i) tick(false, true);
                require(!READ(ready_pcs_out), "stopped reference retained processing readiness");
            } else {
                require(!was_ready && READ(fault_out), "untunable actuator was not rejected");
                ++rejected;
            }
            tick(false, false, true);
            require(READ(clear_out) && !READ(ready_ref_out) && !READ(ready_pcs_out) &&
                    !READ(fault_out) && READ(measured_edges_out) == 0 &&
                    READ(coarse_n_out) == 127 && READ(fine_n_out) == 255,
                    "warm reset failed while both clocks were stopped");
            tick(false, true, true);
            tick(true, false, true);
            enable = false; tick(false, false);
            require(READ(clear_out) && !READ(ready_ref_out) && !READ(ready_pcs_out), "disable failed to stop clock usage");
        }
        check_clock_load_steps(trace, events);
        check_clock_drift(trace, events);
        require(bool(trace), "trace write failed");
        std::cout << "PASS processing clock: " << qualified << " tuned curves, " << rejected << " rejected curves, 6 load-step cases, 2 drift recoveries; "
                  << events << " clock events\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
