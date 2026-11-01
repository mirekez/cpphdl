#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "ethernet_frame_reference.hpp"
#ifdef VERILATOR
#include "VNegotiatedRepeater.h"
using Device = VNegotiatedRepeater;
#else
#include "../negotiated_repeater.hpp"
using Device = NegotiatedRepeater;
long _system_clock = -1;
#endif

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Bench {
    Device dut;
    bool enable = true, restart_a = false, restart_b = false;
    bool pulse_a = false, pulse_b = false, pma = false, ready = false, valid = false;
    unsigned rx_a = 0, rx_b = 0, tx_a = 0, tx_b = 0, status = 0;
    std::uint64_t digest = 1469598103934665603ull, observations = 0;
    Bench() {
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(enable);
        dut.restart_a_in = _ASSIGN(restart_a);
        dut.restart_b_in = _ASSIGN(restart_b);
        dut.pulse_a_in = _ASSIGN(pulse_a);
        dut.pulse_b_in = _ASSIGN(pulse_b);
        dut.pma_a_link_in = _ASSIGN(pma);
        dut.pma_b_link_in = _ASSIGN(pma);
        dut.configuration_a_in = _ASSIGN(12);
        dut.configuration_b_in = _ASSIGN(8);
        dut.seed_a_in = _ASSIGN(0x317);
        dut.seed_b_in = _ASSIGN(0x5aa);
        dut.address_a_in = _ASSIGN(u<48>(0x020000000001ull));
        dut.address_b_in = _ASSIGN(u<48>(0x020000000002ull));
        dut.rx_a_in = _ASSIGN(rx_a);
        dut.rx_b_in = _ASSIGN(rx_b);
        dut.valid_a_in = _ASSIGN(valid);
        dut.valid_b_in = _ASSIGN(valid);
        dut.fault_a_in = _ASSIGN(false);
        dut.fault_b_in = _ASSIGN(false);
        dut.rx_a_ready_in = _ASSIGN(ready);
        dut.rx_b_ready_in = _ASSIGN(ready);
        dut.tx_a_ready_in = _ASSIGN(ready);
        dut.tx_b_ready_in = _ASSIGN(ready);
        dut._assign();
#endif
        step(31, true, true);
    }

    // Bits 0..3 are the four datapath clocks; bit 4 is the common reference.
    void step(unsigned clocks, bool reset = false, bool clear = false) {
#ifdef VERILATOR
        dut.ref_clk = dut.rx_clk = dut.b_rx_clk = dut.a_tx_clk = dut.tx_clk = 0;
        dut.reset = reset;
        dut.enable_in = enable;
        dut.restart_a_in = restart_a;
        dut.restart_b_in = restart_b;
        dut.pulse_a_in = pulse_a;
        dut.pulse_b_in = pulse_b;
        dut.pma_a_link_in = dut.pma_b_link_in = pma;
        dut.configuration_a_in = 12; dut.configuration_b_in = 8;
        dut.seed_a_in = 0x317; dut.seed_b_in = 0x5aa;
        dut.address_a_in = 0x020000000001ull; dut.address_b_in = 0x020000000002ull;
        dut.rx_a_in = rx_a; dut.rx_b_in = rx_b;
        dut.valid_a_in = dut.valid_b_in = valid;
        dut.fault_a_in = dut.fault_b_in = false;
        dut.rx_a_ready_in = dut.rx_b_ready_in = dut.tx_a_ready_in = dut.tx_b_ready_in = ready;
        dut.eval();
        dut.rx_clk = clocks & 1;
        dut.b_rx_clk = (clocks >> 1) & 1;
        dut.a_tx_clk = (clocks >> 2) & 1;
        dut.tx_clk = (clocks >> 3) & 1;
        dut.ref_clk = (clocks >> 4) & 1;
        dut.eval();
#define READ(name) unsigned(dut.name)
#else
        ++_system_clock;
        if (clocks & 1) dut._work_rx_clk(reset);
        if (clocks & 2) dut._work_b_rx_clk(reset);
        if (clocks & 4) dut._work_a_tx_clk(reset);
        if (clocks & 8) dut._work_tx_clk(reset);
        if (clocks & 16) dut._work_ref_clk(reset);
        if (clear) dut.sim_assert_reset();
        if ((clocks & 1) || clear) dut._strobe_rx_clk();
        if ((clocks & 2) || clear) dut._strobe_b_rx_clk();
        if ((clocks & 4) || clear) dut._strobe_a_tx_clk();
        if ((clocks & 8) || clear) dut._strobe_tx_clk();
        if (clocks & 16) dut._strobe_ref_clk();
        ++_system_clock;
#define READ(name) unsigned(dut.name())
#endif
        // A loop cable joins the two negotiation interfaces. The subsequent
        // GMII sources independently exercise both directions of the datapath.
        pulse_a = READ(pulse_b_out) != 0;
        pulse_b = READ(pulse_a_out) != 0;
        status = READ(pma_a_enable_out) | (READ(pma_b_enable_out) << 1) |
                 (READ(master_a_out) << 2) | (READ(master_b_out) << 3) |
                 (READ(link_a_out) << 4) | (READ(link_b_out) << 5) |
                 (READ(pause_a_enabled_out) << 6) | (READ(pause_b_enabled_out) << 7) |
                 (READ(admitted_out) << 8) | (READ(fault_out) << 9);
        tx_a = READ(tx_a_out); tx_b = READ(tx_b_out);
        for (unsigned value : {status, unsigned(pulse_a), unsigned(pulse_b), tx_a, tx_b}) {
            digest ^= value; digest *= 1099511628211ull;
        }
        ++observations;
        require(!(status & 512), "negotiation or forwarding fault");
#undef READ
    }
};

static std::vector<unsigned> data_frame(unsigned salt) {
    auto data = frame(0, 0x020000000003ull);
    data.resize(68);
    data[20] = 8; data[21] = 0;
    for (unsigned i = 22; i < data.size(); ++i) data[i] = (i * 17 + salt) & 255;
    append_fcs(data);
    return data;
}

int main() {
    try {
        Bench bench;
        unsigned trained = 0, completed = 0, first_pulse = 0;
        for (unsigned cycle = 0; cycle < 300000000; ++cycle) {
            bench.step(16);
            require(!bench.tx_a && !bench.tx_b, "traffic escaped before negotiation");
            if (!first_pulse && (bench.pulse_a || bench.pulse_b)) first_pulse = cycle;
            if ((bench.status & 3) == 3 && ++trained == 1000) bench.pma = true;
            if (bench.status & 256) {
                require((bench.status & 255) == 247, "negotiated roles, link or symmetric PAUSE incorrect");
                completed = cycle + 1;
                break;
            }
            if (cycle && cycle % 25000000 == 0) std::cerr << "negotiated repeater reference " << cycle << '\n';
        }
        require(first_pulse >= 162500000 && first_pulse <= 162500010, "FLP did not observe 1300 ms interval");
        require(completed >= 264000000 && completed < 266000000, "negotiation did not complete at physical reference rate");
        bench.ready = true;
        bench.valid = true;
        for (unsigned i = 0; i < 64; ++i) bench.step(31);

        const auto pause = frame(128), a = data_frame(7), b = data_frame(93);
        std::vector<unsigned> received_a, received_b;
        unsigned first_a = 0, first_b = 0;
        for (unsigned cycle = 0; cycle < 10000; ++cycle) {
            bench.rx_a = cycle < pause.size() ? 256 | pause[cycle] : 0;
            bench.rx_b = 0;
            if (cycle >= 200 && cycle < 200 + a.size()) {
                bench.rx_a = 256 | a[cycle - 200];
                bench.rx_b = 256 | b[cycle - 200];
            }
            bench.step(31);
            require(!(bench.tx_a & 512) && !(bench.tx_b & 512), "frame aborted after negotiated admission");
            if (bench.tx_a & 256) {
                if (received_a.empty()) first_a = cycle;
                received_a.push_back(bench.tx_a & 255);
            }
            if (bench.tx_b & 256) {
                if (received_b.empty()) first_b = cycle;
                received_b.push_back(bench.tx_b & 255);
            }
        }
        require(received_a == b && received_b == a, "negotiated forwarding changed data or leaked MAC Control");
        require(first_a > 8192 && first_b >= 200 && first_b < 400, "PAUSE timer was not connected to the negotiated port");

        // Clock-independent link loss must clear both directions immediately.
        bench.pma = false;
        bench.rx_a = bench.rx_b = 0x155;
        bench.step(0, false, true);
        require(!(bench.status & 256) && !bench.tx_a && !bench.tx_b, "stopped-clock PMA loss left forwarding enabled");
        for (unsigned i = 0; i < 16; ++i) bench.step(31, false, true);
        require(!(bench.status & 0x1f0), "PMA loss retained negotiated link/PAUSE status");
        bench.restart_a = true;
        bench.restart_b = true;
        bench.step(0, false, true);
        require(!bench.tx_a && !bench.tx_b, "restart leaked stale data");
        std::cout << "PASS negotiated repeater " << completed << " reference cycles 2 intact frames PAUSE and stopped-clock loss "
                  << bench.observations << " observations " << bench.digest << " digest\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
