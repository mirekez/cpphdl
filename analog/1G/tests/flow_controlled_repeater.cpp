#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include "ethernet_frame_reference.hpp"
#ifdef VERILATOR
#include "VFlowControlledRepeater.h"
using DUT = VFlowControlledRepeater;
#else
#include "../flow_controlled_repeater.hpp"
using DUT = FlowControlledRepeater;
long _system_clock = -1;
#endif

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::vector<unsigned> data_frame(unsigned number, unsigned salt) {
    unsigned size = number % 3 == 0 ? 64 : 1522;
    auto bytes = frame(0);
    bytes.resize(size + 4, 0); // Eight preamble bytes, body, then append four FCS bytes.
    bytes[20] = 8; bytes[21] = 0;
    for (unsigned i = 22; i < bytes.size(); ++i) bytes[i] = (i * 73 + number * 19 + salt) & 255;
    append_fcs(bytes);
    return bytes;
}

struct Peer {
    unsigned salt, limit, supplied = 0, received = 0, index = 0, gap = 12, pauses = 0, resumes = 0;
    unsigned output_gap = 12, paused_symbols = 0;
    std::uint64_t pause_until = 0;
    bool initial = true;
    std::vector<unsigned> sending, collecting;
    std::deque<std::pair<std::uint64_t, unsigned>> commands;
    explicit Peer(unsigned value, unsigned count) : salt(value), limit(count) {}

    unsigned symbol(std::uint64_t time) {
        while (!commands.empty() && commands.front().first <= time) {
            pause_until = time + std::uint64_t(commands.front().second) * 5120000ull; // 0.1 ps units.
            commands.pop_front();
        }
        if (index < sending.size()) return 256 | sending[index++];
        if (!sending.empty()) { sending.clear(); index = 0; gap = 12; }
        if (gap) { --gap; return 0; }
        if (initial) {
            sending = frame(128); // Both peers pause data initially; control must bypass it.
            initial = false;
        } else if (supplied < limit && time >= pause_until) {
            sending = data_frame(supplied++, salt);
        } else {
            if (supplied < limit) ++paused_symbols;
            return 0;
        }
        return 256 | sending[index++];
    }

    void observe(unsigned word, std::uint64_t time, unsigned other_salt, std::uint64_t local_mac) {
        if (word & 256) {
            require((word & 512) == 0, "TX_ER during lossless traffic");
            if (collecting.empty()) require(output_gap >= 12, "short wire IFG");
            collecting.push_back(word & 255);
            output_gap = 0;
        } else {
            ++output_gap;
            if (collecting.empty()) return;
            require(collecting.size() >= 72, "truncated output frame");
            auto bytes = collecting;
            auto fcs = std::uint32_t(bytes[bytes.size() - 4]) | (std::uint32_t(bytes[bytes.size() - 3]) << 8) |
                       (std::uint32_t(bytes[bytes.size() - 2]) << 16) | (std::uint32_t(bytes.back()) << 24);
            bytes.resize(bytes.size() - 4);
            require(crc(bytes, 8) == fcs, "invalid output FCS");
            if (collecting[20] == 0x88 && collecting[21] == 8) {
                unsigned quanta = (collecting[24] << 8) | collecting[25];
                auto expected = frame(quanta);
                for (unsigned i = 0; i < 6; ++i) expected[14 + i] = (local_mac >> ((5 - i) * 8)) & 255;
                expected.resize(68); append_fcs(expected);
                require(collecting == expected, "forwarded peer PAUSE or malformed generated control");
                if (quanta) ++pauses; else ++resumes;
                // Cable, peer receiver and MAC reaction latency after the FCS.
                commands.emplace_back(time + 7500000ull, quanta);
            } else {
                require(received < limit && collecting == data_frame(received, other_salt), "dropped/reordered/corrupted data frame");
                ++received;
            }
            collecting.clear();
        }
    }
};

struct Bench {
    DUT dut;
    unsigned a = 0, b = 0, out_a = 0, out_b = 0, faults = 0;
    bool valid_a = false, valid_b = false, enable = false, capable_a = true, capable_b = true;
    std::uint64_t cycles = 0, hash = 1469598103934665603ull;
    Bench() {
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(enable);
        dut.pause_a_enabled_in = _ASSIGN(capable_a); dut.pause_b_enabled_in = _ASSIGN(capable_b);
        dut.address_a_in = _ASSIGN(u<48>(0x020000000001ull)); dut.address_b_in = _ASSIGN(u<48>(0x020000000002ull));
        dut.rx_a_in = _ASSIGN(a); dut.rx_b_in = _ASSIGN(b);
        dut.valid_a_in = _ASSIGN(valid_a); dut.valid_b_in = _ASSIGN(valid_b);
        dut.fault_a_in = _ASSIGN(false); dut.fault_b_in = _ASSIGN(false);
        dut.rx_a_ready_in = _ASSIGN(true); dut.rx_b_ready_in = _ASSIGN(true);
        dut.tx_a_ready_in = _ASSIGN(true); dut.tx_b_ready_in = _ASSIGN(true);
        dut._assign();
#endif
        step(15, true);
        enable = true;
        for (unsigned i = 0; i < 12; ++i) step(15);
    }
    void step(unsigned clocks, bool reset = false) {
#ifdef VERILATOR
        dut.rx_clk = dut.b_rx_clk = dut.a_tx_clk = dut.tx_clk = 0;
        dut.reset = reset; dut.enable_in = enable;
        dut.pause_a_enabled_in = capable_a; dut.pause_b_enabled_in = capable_b;
        dut.address_a_in = 0x020000000001ull; dut.address_b_in = 0x020000000002ull;
        dut.rx_a_in = a; dut.rx_b_in = b; dut.valid_a_in = valid_a; dut.valid_b_in = valid_b;
        dut.fault_a_in = dut.fault_b_in = false;
        dut.rx_a_ready_in = dut.rx_b_ready_in = dut.tx_a_ready_in = dut.tx_b_ready_in = true;
        dut.eval();
        dut.rx_clk = clocks & 1; dut.b_rx_clk = (clocks >> 1) & 1;
        dut.a_tx_clk = (clocks >> 2) & 1; dut.tx_clk = (clocks >> 3) & 1;
        dut.eval();
#define READ(name) unsigned(dut.name)
#else
        ++_system_clock;
        if (clocks & 1) dut._work_rx_clk(reset);
        if (clocks & 2) dut._work_b_rx_clk(reset);
        if (clocks & 4) dut._work_a_tx_clk(reset);
        if (clocks & 8) dut._work_tx_clk(reset);
        bool clear = reset || !enable || !capable_a || !capable_b;
        if (clear) dut.sim_assert_reset();
        if ((clocks & 1) || clear) dut._strobe_rx_clk();
        if ((clocks & 2) || clear) dut._strobe_b_rx_clk();
        if ((clocks & 4) || clear) dut._strobe_a_tx_clk();
        if ((clocks & 8) || clear) dut._strobe_tx_clk();
        ++_system_clock;
#define READ(name) unsigned(dut.name())
#endif
        out_a = READ(tx_a_out); out_b = READ(tx_b_out); faults = READ(fault_out);
        for (unsigned value : {clocks, a, b, unsigned(valid_a), unsigned(valid_b), out_a, out_b, faults,
                               READ(pause_a_out), READ(pause_b_out), READ(control_a_sent_out), READ(control_b_sent_out)}) {
            hash ^= value; hash *= 1099511628211ull;
        }
#undef READ
        ++cycles;
    }
};

int main() {
    try {
        unsigned total_frames = 0, total_pauses = 0, total_resumes = 0;
        std::uint64_t total_cycles = 0, total_digest = 0;
        for (unsigned variant = 0; variant < 3; ++variant) {
            Bench bench;
            Peer a(17, 40), b(93, 40);
            std::array<std::uint64_t, 4> next{0, 35000 + variant, 11000, 57000};
            std::array<unsigned, 4> periods{74630, 75188, 80008, 80008};
            std::uint64_t symbol_a = 0, symbol_b = 0;
            unsigned quiet = 0;
            while (quiet < 4096) {
                auto time = *std::min_element(next.begin(), next.end());
                require(time < 20000000000ull, "flow-controlled forwarding deadlocked");
                unsigned clocks = 0;
                for (unsigned i = 0; i < 4; ++i)
                    if (time == next[i]) { clocks |= 1u << i; next[i] += periods[i]; }
                bench.valid_a = (clocks & 1) && time >= symbol_a;
                bench.valid_b = (clocks & 2) && time >= symbol_b;
                if (bench.valid_a) { bench.a = a.symbol(time); symbol_a += 79992 + variant * 8; }
                if (bench.valid_b) { bench.b = b.symbol(time); symbol_b += 80000 - variant * 4; }
                bench.step(clocks);
                require(bench.faults == 0, "forwarding overflow/underflow during negotiated flow control");
                if (clocks & 4) a.observe(bench.out_a, time, b.salt, 0x020000000001ull);
                if (clocks & 8) b.observe(bench.out_b, time, a.salt, 0x020000000002ull);
                if (a.received == 40 && b.received == 40) ++quiet;
            }
            require(a.pauses && b.pauses && a.resumes && b.resumes && a.paused_symbols && b.paused_symbols,
                    "did not exercise bilateral pause/resume with blocked data transmitters");
            total_frames += a.received + b.received;
            total_pauses += a.pauses + b.pauses; total_resumes += a.resumes + b.resumes;
            total_cycles += bench.cycles; total_digest = (total_digest ^ bench.hash) * 1099511628211ull;
        }
        // Neither unsupported PAUSE negotiation nor stopped-clock disable
        // may leak frames. Releasing reset must clear old commands and data.
        for (unsigned blocked_port : {0, 1}) {
            Bench bench;
            bench.capable_a = blocked_port != 0; bench.capable_b = blocked_port != 1;
            bench.a = bench.b = 0x155; bench.valid_a = bench.valid_b = true;
            for (unsigned i = 0; i < 100; ++i) {
                bench.step(15);
                require(!bench.out_a && !bench.out_b && !bench.faults, "unnegotiated PAUSE admitted data");
            }
            bench.enable = false; bench.step(0);
            require(!bench.out_a && !bench.out_b, "stopped-clock disable leaked output");
            bench.capable_a = bench.capable_b = true; bench.valid_a = bench.valid_b = false;
            bench.enable = true;
            for (unsigned i = 0; i < 16; ++i) {
                bench.step(i < 8 ? 3 : 12);
                require(!bench.out_a && !bench.out_b, "stale data leaked during clock restart");
            }
            for (unsigned i = 0; i < 100; ++i) {
                bench.step(15);
                require(!bench.out_a && !bench.out_b && !bench.faults, "stale command survived reset");
            }
            total_cycles += bench.cycles; total_digest = (total_digest ^ bench.hash) * 1099511628211ull;
        }
        std::cout << "PASS 3 duplex cases " << total_frames << " intact frames " << total_pauses << " pauses "
                  << total_resumes << " resumes 2 isolation cases " << total_cycles << " cycles " << total_digest << " digest\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
