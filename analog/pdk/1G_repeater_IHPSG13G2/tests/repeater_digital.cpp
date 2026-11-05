#ifdef VERILATOR
#include "VRepeaterDigital.h"
using DUT = VRepeaterDigital;
#define READ(name) unsigned(dut.name)
#else
#include "../repeater_digital.hpp"
long _system_clock = -1;
using DUT = RepeaterDigital;
#define READ(name) unsigned(dut.name())
#endif
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

// Startup and containment at the real two-port digital boundary. Comparator
// noise cannot establish a copper link; each port needs FLP negotiation and
// valid training. This test does not claim an electrical Ethernet link test.
int main() {
    auto storage = std::make_unique<DUT>();
    auto& dut = *storage;
    bool enable = false, power_good = false, restart[2]{};
    unsigned comparisons[8]{};
    unsigned raw_phase = 0;
    std::uint64_t digest = 1469598103934665603ULL;
#ifndef VERILATOR
    dut.enable_in = _ASSIGN(enable); dut.power_good_in = _ASSIGN(power_good);
    for (unsigned i = 0; i < 2; ++i) {
        dut.restart_in[i] = _ASSIGN_I(restart[i]);
        dut.signal_ready_in[i] = _ASSIGN(true);
        dut.tx_clock_ready_in[i] = _ASSIGN(true);
        dut.pulse_in[i] = _ASSIGN(false);
        dut.configuration_in[i] = _ASSIGN(0);
        dut.negotiation_seed_in[i] = _ASSIGN_I(13 + i);
        dut.scrambler_seed_in[i] = _ASSIGN_I(1 + i);
        dut.address_in[i] = _ASSIGN_I(0x020000000001ULL + i);
        dut.local_history_in[i] = _ASSIGN(1170);
    }
    for (unsigned i = 0; i < 8; ++i) {
        dut.comparisons_in[i] = _ASSIGN_I(comparisons[i]);
        dut.offset_in[i] = _ASSIGN(128);
        dut.gain_in[i] = _ASSIGN(256);
    }
    dut._assign();
#endif
    for (unsigned time = 0; time < 400000; ++time) {
        const bool reset = time < 32 || (time >= 200000 && time < 200032);
        const bool ref = time % 8 == 0, adc = time % 4 == 0;
        // A valid independently scheduled 134 MHz raw clock must qualify
        // before the delivered PCS clock starts. Its source is a test input;
        // transistor oscillator loading and trim response are separate tests.
        raw_phase += 134;
        const bool raw = raw_phase >= 1000;
        if (raw) raw_phase -= 1000;
        const bool pcs = raw && READ(processing_admit_out);
        const bool tx_a = time % 8 == 1, tx_b = time % 9 == 0;
        enable = time >= 64 && time < 360000;
        power_good = time >= 48 && !(time >= 180000 && time < 190000);
        restart[0] = time >= 240000 && time < 241000;
        restart[1] = time >= 280000 && time < 281000;
        for (unsigned i = 0; i < 8; ++i) comparisons[i] = ((time * 1103515245u) ^ (i * 12345)) & 8191;
#ifdef VERILATOR
        dut.ref_clk = dut.adc_clk = dut.raw_clk = dut.pcs_clk = dut.tx_a_clk = dut.tx_b_clk = 0;
        dut.reset = reset; dut.enable_in = enable; dut.power_good_in = power_good;
        for (unsigned i = 0; i < 2; ++i) {
            dut.restart_in[i] = restart[i]; dut.signal_ready_in[i] = 1;
            dut.tx_clock_ready_in[i] = 1; dut.pulse_in[i] = 0;
            dut.configuration_in[i] = 0; dut.negotiation_seed_in[i] = 13 + i;
            dut.scrambler_seed_in[i] = 1 + i; dut.address_in[i] = 0x020000000001ULL + i;
            dut.local_history_in[i] = 1170;
        }
        for (unsigned i = 0; i < 8; ++i) {
            dut.comparisons_in[i] = comparisons[i]; dut.offset_in[i] = 128; dut.gain_in[i] = 256;
        }
        dut.eval();
        dut.ref_clk = ref; dut.adc_clk = adc; dut.raw_clk = raw;
        dut.pcs_clk = pcs; dut.tx_a_clk = tx_a; dut.tx_b_clk = tx_b;
        dut.eval();
#else
        ++_system_clock;
        if (ref) dut._work_ref_clk(reset);
        if (adc) dut._work_adc_clk(reset);
        if (raw) dut._work_raw_clk(reset);
        if (pcs) dut._work_pcs_clk(reset);
        if (tx_a) dut._work_tx_a_clk(reset);
        if (tx_b) dut._work_tx_b_clk(reset);
        if (reset) dut.sim_assert_reset();
        if (ref || reset) dut._strobe_ref_clk();
        if (adc || reset) dut._strobe_adc_clk();
        if (raw || reset) dut._strobe_raw_clk();
        if (pcs || reset) dut._strobe_pcs_clk();
        if (tx_a || reset) dut._strobe_tx_a_clk();
        if (tx_b || reset) dut._strobe_tx_b_clk();
        ++_system_clock;
#endif
        if (time < 40) continue;
        if ((time == 160000 || time == 340000) &&
            (!READ(adc_ready_out) || !READ(processing_ready_out)))
            throw std::runtime_error("valid clocks failed qualification/restart at " + std::to_string(time) +
                " adc=" + std::to_string(READ(adc_ready_out)) + " pcs=" + std::to_string(READ(processing_ready_out)) +
                " admit=" + std::to_string(READ(processing_admit_out)) + " clear=" + std::to_string(READ(processing_clear_out)) +
                " coarse=" + std::to_string(READ(processing_coarse_n_out)) + " fine=" + std::to_string(READ(processing_fine_n_out)) +
                " fault=" + std::to_string(READ(fault_out)));
        if (READ(active_out) || READ(link_out[0]) || READ(link_out[1]))
            throw std::runtime_error("untrained port admitted link/forwarding");
        for (unsigned i = 0; i < 8; ++i) {
            if (!READ(clear_out[i]) || READ(evaluate_enable_out[i]))
                throw std::runtime_error("analog decision escaped receiver qualification");
            if (READ(main_p_out[i]) || READ(main_n_out[i]) || READ(history_p_out[i]) || READ(history_n_out[i]))
                throw std::runtime_error("PAM5 driver enabled before negotiation");
        }
        for (unsigned i = 0; i < 128; ++i)
            if (READ(acquire_out[i]) || READ(convert_out[i]))
                throw std::runtime_error("SAR acquisition escaped receiver qualification");
        const auto hash = [&](unsigned word) { digest = (digest ^ word) * 1099511628211ULL; };
        hash(READ(adc_ready_out)); hash(READ(processing_ready_out));
        hash(READ(processing_coarse_n_out)); hash(READ(processing_fine_n_out));
        hash(READ(processing_clear_out)); hash(READ(processing_admit_out)); hash(READ(fault_out));
        for (unsigned i = 0; i < 2; ++i) {
            hash(READ(pulse_out[i])); hash(READ(pulse_enable_out[i]));
            hash(READ(master_out[i])); hash(READ(symbols_out[i]));
        }
    }
    std::cout << "PASS repeater digital startup: two PHYs, six clocks, no premature link or drive; "
              << digest << " digest\n";
}
