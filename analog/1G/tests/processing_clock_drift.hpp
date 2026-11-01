#pragma once
#include <ostream>
#include <stdexcept>

// Move a tunable actuator beyond each edge of the admitted band. Withdrawal
// alone is insufficient: the controller must retune in the correct direction
// and regain a stable, in-band clock without an external reset.
inline void check_clock_drift(std::ostream& trace, unsigned& events) {
    for (unsigned profile = 0; profile < 2; ++profile) {
        DUT dut;
        bool enable = true;
        unsigned trim = 32768, before = 0;
        double phase = profile ? .7 : .3;
        bool withdrew = false, moved = false, recovered = false;
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(enable);
        dut._assign();
#else
        dut.ref_clk = dut.pcs_clk = dut.reset = 0;
        dut.enable_in = enable;
        dut.eval();
#endif
        auto tick = [&](bool ref, bool pcs, bool reset) {
#ifdef VERILATOR
            dut.ref_clk = dut.pcs_clk = 0;
            dut.reset = reset;
            dut.eval();
            dut.ref_clk = ref; dut.pcs_clk = pcs;
            dut.eval();
#else
            ++_system_clock;
            if (reset) {
                dut._reset_pos_ref_clk(); dut._reset_pos_pcs_clk();
            } else {
                if (ref) dut._work_ref_clk(false);
                if (pcs) dut._work_pcs_clk(false);
            }
            if (ref || reset) dut._strobe_ref_clk();
            if (pcs || reset) dut._strobe_pcs_clk();
            ++_system_clock;
#endif
            trim = ((READ(coarse_n_out) ^ 255) << 8) | (READ(fine_n_out) ^ 255);
            trace << events++ << ',' << profile + 14 << ',' << ref << ',' << pcs << ',' << reset << ','
                  << READ(coarse_n_out) << ',' << READ(fine_n_out) << ',' << READ(clear_out) << ','
                  << READ(ready_ref_out) << ',' << READ(ready_pcs_out) << ',' << READ(fault_out) << ','
                  << READ(measured_edges_out) << '\n';
        };
        tick(false, false, true);
        for (unsigned ns = 1; ns < 210000; ++ns) {
            double frequency = unloaded_frequency(trim) + (ns >= 60000 ? (profile ? 6 : -6) : 0);
            if (frequency < 0) frequency = 0;
            if (READ(clear_out)) phase = 0;
            else phase += frequency / 1000;
            bool pcs = phase >= 1;
            if (pcs) phase -= 1;
            bool ref = ns % 8 == profile;
            if (ref || pcs) tick(ref, pcs, false);
            if (ns == 59999) {
                if (!READ(ready_ref_out) || !READ(ready_pcs_out))
                    throw std::runtime_error("clock never acquired before drift");
                before = trim;
            }
            if (ns >= 60000 && !READ(ready_ref_out) && !READ(ready_pcs_out)) withdrew = true;
            if (ns >= 60000 && !moved && trim != before) {
                if (!withdrew || (profile ? trim >= before : trim <= before))
                    throw std::runtime_error("clock drift retuned in the wrong direction or before withdrawal");
                moved = true;
            }
            if (ns >= 160000) {
                if (!READ(ready_ref_out) || !READ(ready_pcs_out) || READ(fault_out) ||
                    frequency <= 132 || frequency >= 136)
                    throw std::runtime_error("clock failed to reacquire after tunable frequency drift");
                recovered = true;
            }
        }
        if (!withdrew || !moved || !recovered)
            throw std::runtime_error("clock drift recovery scenario incomplete");
    }
}
