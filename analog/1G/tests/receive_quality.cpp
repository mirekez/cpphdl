#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VReceiveQuality.h"
using DUT = VReceiveQuality;
#else
#include "../receive_quality.hpp"
using DUT = ReceiveQuality;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected complete-clock trace");
        std::ofstream trace(argv[1]);
        if (!trace)
            throw std::runtime_error("cannot open trace");
        DUT dut;
        unsigned word = 0, model_count = 0, model_flushed = 0, clocks = 0, acquisitions = 0, revocations = 0;
        bool valid = false, locked = true, fault = false, clipped = false, enable = true, model_ready = false;
#ifndef VERILATOR
        dut.receive_in = _ASSIGN(word);
        dut.valid_in = _ASSIGN(valid);
        dut.locked_in = _ASSIGN(locked);
        dut.fault_in = _ASSIGN(fault);
        dut.clipped_in = _ASSIGN(clipped);
        dut.enable_in = _ASSIGN(enable);
        dut._assign();
#endif
        auto tick = [&](bool reset = false) {
            const bool bad = !enable || !locked || fault || clipped || (valid && (word & 512));
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.receive_in = word;
            dut.valid_in = valid;
            dut.locked_in = locked;
            dut.fault_in = fault;
            dut.clipped_in = clipped;
            dut.enable_in = enable;
            dut.eval();
            const bool ready = dut.ready_out;
            const unsigned count = dut.idles_out, flushed = dut.flushed_out;
#else
            ++_system_clock;
            const bool ready = dut.ready_out();
            const unsigned count = unsigned(dut.idles_out()), flushed = unsigned(dut.flushed_out());
#endif
            if (ready != (model_ready && !bad) || count != model_count || flushed != model_flushed)
                throw std::runtime_error("qualification state or immediate revocation differs from independent model");
            trace << clocks++ << ',' << word << ',' << valid << ',' << locked << ',' << fault << ',' << clipped << ','
                  << enable << ',' << reset << ',' << ready << ',' << count << ',' << flushed << '\n';
            if (bad || reset) {
                revocations += model_ready;
                model_count = 0;
                model_flushed = 0;
                model_ready = false;
            } else if (valid && model_flushed < 39) {
                ++model_flushed;
            } else if (valid && !model_ready) {
                model_count = word == 0 ? model_count + 1 : 0;
                if (model_count == 64) {
                    model_ready = true;
                    ++acquisitions;
                }
            }
#ifdef VERILATOR
            dut.clk = 1;
            dut.eval();
#else
            dut._work(reset);
            dut._strobe();
#endif
        };
        tick(true);
        // Establish quality with intermittent accepted symbols. Poisoned words
        // during invalid clocks cannot count or revoke it.
        for (unsigned reason = 0; reason < 7; ++reason) {
            for (unsigned n = 0; n < 39; ++n) {
                valid = true;
                word = 0;
                tick();
            }
            for (unsigned n = 0; n < 64; ++n) {
                valid = false;
                word = 1023;
                for (unsigned gap = 0; gap < n % 4; ++gap)
                    tick();
                valid = true;
                word = 0;
                tick();
            }
            valid = true;
            word = 256 | 93;
            for (unsigned n = 0; n < 23; ++n)
                tick();
            valid = reason == 0;
            word = reason == 0 ? 512 : 0;
            locked = reason != 1;
            fault = reason == 2;
            clipped = reason == 3;
            enable = reason != 4;
            tick(reason == 5 || reason == 6);
            locked = enable = true;
            fault = clipped = false;
            valid = false;
            word = 0;
            tick();
        }
        for (unsigned n = 0; n < 39; ++n) {
            valid = true;
            word = 0;
            tick();
        }
        // A data word before qualification breaks consecutive-idle evidence.
        for (unsigned n = 0; n < 63; ++n) {
            valid = true;
            word = 0;
            tick();
        }
        word = 256;
        tick();
        for (unsigned n = 0; n < 64; ++n) {
            word = 0;
            tick();
        }
        tick(true);
        // A fault during fill must restart the entire accepted-word flush.
        for (unsigned reason = 0; reason < 4; ++reason) {
            for (unsigned n = 0; n < 17; ++n) {
                valid = true;
                word = 0;
                tick();
            }
            fault = reason == 0;
            clipped = reason == 1;
            locked = reason != 2;
            word = reason == 3 ? 512 : 0;
            tick();
            fault = clipped = false;
            locked = true;
            valid = false;
            word = 0;
            tick(true);
        }
        if (acquisitions != 8 || revocations != 8)
            throw std::runtime_error("missing acquisition/revocation scenario");
        std::cout << "PASS " << clocks << " clocks, " << acquisitions << " acquisitions, " << revocations
                  << " revocations; symbol-count qualification and reset/fault priority\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
