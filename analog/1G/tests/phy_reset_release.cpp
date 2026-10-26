#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#ifdef TEST_CLOCK_QUALIFICATION
#include "VPHYResetControl.h"
using DUT = VPHYResetControl;
#else
#include "VPHYResetRelease.h"
using DUT = VPHYResetRelease;
#endif
#define READ_RESET(name) unsigned(dut.name)
#else
#ifdef TEST_CLOCK_QUALIFICATION
#include "../phy_reset_control.hpp"
using DUT = PHYResetControl;
#define RESET_TARGET dut.resets
#else
#include "../phy_reset_release.hpp"
using DUT = PHYResetRelease;
#define RESET_TARGET dut
#endif
long _system_clock = -1;
#define READ_RESET(name) unsigned(dut.name())
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected trace destination");
        std::ofstream trace(argv[1]);
        DUT dut;
        bool was_reset = false;
        unsigned edges[3]{}, events = 0;
        unsigned withdrawal = 0;
#ifndef VERILATOR
#ifdef TEST_CLOCK_QUALIFICATION
        dut.enable_in = _ASSIGN(!(withdrawal & 1));
        dut.power_good_in = _ASSIGN(!(withdrawal & 2));
        dut.adc_ready_in = _ASSIGN(!(withdrawal & 4));
        dut.pcs_ready_in = _ASSIGN(!(withdrawal & 8));
#endif
        dut._assign();
#endif
        auto edge_event = [&](bool reset, unsigned clocks, unsigned lost) {
            withdrawal = lost;
            const bool asserted = reset || lost;
            for (unsigned d = 0; d < 3; ++d) {
                if (asserted)
                    edges[d] = 0;
                else if (((clocks >> d) & 1) && edges[d] < 2)
                    ++edges[d];
            }
#ifdef VERILATOR
            dut.ref_clk = 0; dut.adc_clk = 0; dut.pcs_clk = 0;
            dut.reset = reset;
#ifdef TEST_CLOCK_QUALIFICATION
            dut.enable_in = !(lost & 1); dut.power_good_in = !(lost & 2);
            dut.adc_ready_in = !(lost & 4); dut.pcs_ready_in = !(lost & 8);
#endif
            dut.eval();
            dut.ref_clk = clocks & 1; dut.adc_clk = (clocks >> 1) & 1; dut.pcs_clk = (clocks >> 2) & 1;
            dut.eval();
#else
            ++_system_clock;
            // Native event dispatch follows the child's combined physical
            // reset, including a qualification pulse with every clock stopped.
            if (asserted && !was_reset) {
                RESET_TARGET._reset_pos_ref_clk(); RESET_TARGET._reset_pos_adc_clk(); RESET_TARGET._reset_pos_pcs_clk();
                dut._strobe_ref_clk(); dut._strobe_adc_clk(); dut._strobe_pcs_clk();
            }
            if (clocks & 1) {
                if (asserted) RESET_TARGET._reset_pos_ref_clk();
                else dut._work_ref_clk(false);
            }
            if (clocks & 2) {
                if (asserted) RESET_TARGET._reset_pos_adc_clk();
                else dut._work_adc_clk(false);
            }
            if (clocks & 4) {
                if (asserted) RESET_TARGET._reset_pos_pcs_clk();
                else dut._work_pcs_clk(false);
            }
            if (clocks & 1) dut._strobe_ref_clk();
            if (clocks & 2) dut._strobe_adc_clk();
            if (clocks & 4) dut._strobe_pcs_clk();
            ++_system_clock;
#endif
            was_reset = asserted;
            if (READ_RESET(reset_ref_out) != unsigned(edges[0] < 2) ||
                READ_RESET(reset_adc_out) != unsigned(edges[1] < 2) ||
                READ_RESET(reset_pcs_out) != unsigned(edges[2] < 2))
                throw std::runtime_error("early release, missed asynchronous assertion or stopped-clock transition");
            trace << events++ << ',' << (unsigned(reset) | (lost << 1)) << ',' << clocks << ',' << READ_RESET(reset_ref_out) << ','
                  << READ_RESET(reset_adc_out) << ',' << READ_RESET(reset_pcs_out) << '\n';
        };
        // Every three-event combination of independent domain edges, with
        // reset withdrawal at each boundary, plus stopped-clock reset pulses.
        const unsigned sources =
#ifdef TEST_CLOCK_QUALIFICATION
            5;
#else
            1;
#endif
        for (unsigned source = 0; source < sources; ++source) {
            auto event = [&](bool requested, unsigned clocks) {
                edge_event(requested && source == 0, clocks,
                           requested && source > 0 ? 1u << (source - 1) : 0);
            };
            for (unsigned pattern = 0; pattern < 512; ++pattern) {
                for (unsigned release = 0; release <= 3; ++release) {
                    event(true, 0);
                    for (unsigned i = 0; i < 3; ++i)
                        event(i < release, (pattern >> (i * 3)) & 7);
                    event(false, 0);
                    for (unsigned domain = 1; domain <= 4; domain <<= 1) {
                        event(false, domain);
                        event(false, domain);
                    }
                    event(true, 0);
                    event(false, 0);
                    event(false, 7);
                    event(false, 7);
                }
            }
        }
        if (!trace)
            throw std::runtime_error("incomplete reset trajectory");
        std::cout << "PASS reset release: " << sources * 2048 << " stop/start schedules; " << events << " events\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
