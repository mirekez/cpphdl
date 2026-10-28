#pragma once
#include <ostream>
#include <stdexcept>
#include <string>

// A monotonic actuator with a small frequency reduction when the delivered
// clock starts switching. This isolates admission feedback from transistor
// simulation; the complete circuit must independently pass its electrical test.
static double unloaded_frequency(unsigned trim) {
    const unsigned code[] = {0, 32768, 36864, 37888, 38400, 38528, 38592,
                            38656, 38912, 40960, 49152, 65535};
    const double mhz[] = {0, 57.057, 108.385, 123.488, 131.352, 133.145,
                          134.001, 135.367, 139.272, 171.084, 295.938, 500};
    for (unsigned i = 1; i < sizeof(code) / sizeof(code[0]); ++i) {
        if (trim <= code[i])
            return mhz[i - 1] + (mhz[i] - mhz[i - 1]) *
                   double(trim - code[i - 1]) / double(code[i] - code[i - 1]);
    }
    throw std::runtime_error("trim outside actuator range");
}

inline void check_clock_load_steps(std::ostream& trace, unsigned& events) {
    for (unsigned profile = 0; profile < 6; ++profile) {
        DUT dut;
        bool enable = true;
        unsigned trim = 32768;
        double phase = double(profile) / 7;
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(enable);
        dut._assign();
#else
        dut.ref_clk = 0;
        dut.pcs_clk = 0;
        dut.reset = 0;
        dut.enable_in = enable;
        dut.eval();
#endif
        auto tick = [&](bool ref, bool pcs, bool reset) {
#ifdef VERILATOR
            dut.ref_clk = 0;
            dut.pcs_clk = 0;
            dut.reset = reset;
            dut.eval();
            dut.ref_clk = ref;
            dut.pcs_clk = pcs;
            dut.eval();
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
            trim = ((READ(coarse_n_out) ^ 255) << 8) | (READ(fine_n_out) ^ 255);
            trace << events++ << ',' << profile + 8 << ',' << ref << ',' << pcs << ',' << reset << ','
                  << READ(coarse_n_out) << ',' << READ(fine_n_out) << ',' << READ(clear_out) << ','
                  << READ(ready_ref_out) << ',' << READ(ready_pcs_out) << ',' << READ(fault_out) << ','
                  << READ(measured_edges_out) << '\n';
        };
        tick(false, false, true);
        bool admitted = false, withdrew = false;
        // 200 us also crosses the raw counter's wrap boundary. The last
        // two profiles then force real frequency faults in both directions.
        for (unsigned ns = 1; ns < 210000; ++ns) {
            double frequency = unloaded_frequency(trim) - (READ(ready_pcs_out) ? .2276 : 0);
            if (profile >= 4 && ns >= 200000)
                frequency = profile == 4 ? 131 : 137;
            if (READ(clear_out)) phase = 0;
            else phase += frequency / 1000;
            const bool pcs = phase >= 1;
            if (pcs) phase -= 1;
            const bool ref = ns % 8 == profile;
            if (ref || pcs) tick(ref, pcs, false);
            if (ns >= 40000 && ns < 200000) {
                if (!READ(ready_ref_out) || !READ(ready_pcs_out) || READ(fault_out))
                    throw std::runtime_error("in-band load step lost readiness at " + std::to_string(ns) + " ns");
                if (!(frequency > 132 && frequency < 136))
                    throw std::runtime_error("admitted frequency violates operating band");
                admitted = true;
            }
            if (profile >= 4 && ns >= 200000 && !READ(ready_ref_out) && !READ(ready_pcs_out))
                withdrew = true;
            if (profile >= 4 && ns == 208400 && !withdrew)
                throw std::runtime_error("out-of-band clock was not withdrawn within two monitor intervals");
        }
        if (!admitted || (profile >= 4 && !withdrew))
            throw std::runtime_error("load-step scenario incomplete");
    }
}
