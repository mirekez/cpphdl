#ifdef VERILATOR
#include "VAutonegotiation.h"
#include <verilated.h>
using Device = VAutonegotiation;
#else
#include "../autonegotiation.hpp"
long _system_clock = -1;
using Device = Autonegotiation;
#endif
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Signals {
    bool pulse = false, enable = true, restart = false, pma = false;
    unsigned config = 0, seed = 0;
};

// Wire-side monitor, independent of the hardware decoder and page controller.
struct Monitor {
    bool previous = false, active = false, data_seen = false;
    unsigned rising = 0, clock = 0, burst = 0, position = 0, word = 0;
    std::vector<unsigned> pages;

    void sample(bool level, unsigned cycle) {
        if (level && !previous) {
            rising = cycle;
            if (!active) {
                if (pages.empty()) {
                    if (cycle < 162500000 || cycle > 162500006)
                        throw std::runtime_error("negotiation did not observe the actual 1300 ms reference interval");
                } else if (cycle - burst != 2000000)
                    throw std::runtime_error("wrong wire burst repetition");
                active = true;
                burst = cycle;
                clock = cycle;
                position = 0;
                word = 0;
                data_seen = false;
            } else if (cycle - clock == 7812 && !data_seen) {
                word |= 1 << position;
                data_seen = true;
            } else if (cycle - clock == 15625) {
                clock = cycle;
                data_seen = false;
                if (++position == 16) {
                    pages.push_back(word);
                    active = false;
                }
            } else
                throw std::runtime_error("wire pulse did not occupy a clock/data position");
        }
        if (!level && previous && cycle - rising != 13)
            throw std::runtime_error("negotiation truncated a physical pulse");
        previous = level;
    }

    void check(unsigned config, unsigned seed) const {
        const std::array<unsigned, 4> expected{0x8001, 0xB808, 0x9000 | config, 0x1800 | seed};
        unsigned group = 0, acknowledged = 0;
        for (unsigned value : pages) {
            const unsigned payload = value & 0xBFFF;
            if (payload != expected[group]) {
                if (acknowledged < 7 || group == 3 || payload != expected[group + 1])
                    throw std::runtime_error("wire pages violated order, role fields, seed or ACK completion");
                ++group;
                acknowledged = 0;
            }
            acknowledged += (value & 0x4000) != 0;
        }
        if (group != 3 || acknowledged < 7 || active || previous)
            throw std::runtime_error("PMA enabled before all four complete page exchanges");
    }
};

int main(int argc, char** argv) {
    try {
        Device left, right;
        Signals a, b;
        a.config = 12; // Multiport, full duplex.
        b.config = 8;  // Single-port, full duplex.
        a.seed = 0x317;
        b.seed = 0x5AA;
#ifndef VERILATOR
        auto bind = [](Device& node, Signals& signals) {
            node.pulse_in = _ASSIGN(signals.pulse);
            node.enable_in = _ASSIGN(signals.enable);
            node.restart_in = _ASSIGN(signals.restart);
            node.pma_link_in = _ASSIGN(signals.pma);
            node.local_up1_in = _ASSIGN(signals.config);
            node.entropy_seed_in = _ASSIGN(signals.seed);
            node._assign();
        };
        bind(left, a);
        bind(right, b);
#endif
        auto edge = [](Device& node, const Signals& signals, bool reset) {
            unsigned result;
#ifdef VERILATOR
            node.clk = 0;
            node.reset = reset;
            node.pulse_in = signals.pulse;
            node.enable_in = signals.enable;
            node.restart_in = signals.restart;
            node.pma_link_in = signals.pma;
            node.local_up1_in = signals.config;
            node.entropy_seed_in = signals.seed;
            node.eval();
            node.clk = 1;
            node.eval();
            result = unsigned(node.pulse_out) | (unsigned(node.pma_enable_out) << 1) |
                     (unsigned(node.link_out) << 2) | (unsigned(node.master_out) << 3) |
                     (unsigned(node.fault_out) << 4) | (unsigned(node.reason_out) << 5);
#else
            (void)signals;
            ++_system_clock;
            node._work(reset);
            node._strobe();
            result = unsigned(node.pulse_out()) | (unsigned(node.pma_enable_out()) << 1) |
                     (unsigned(node.link_out()) << 2) | (unsigned(node.master_out()) << 3) |
                     (unsigned(node.fault_out()) << 4) | (unsigned(node.reason_out()) << 5);
#endif
            if ((result & 4) && !(result & 2))
                throw std::runtime_error("forwarding without a negotiated PMA");
            if (result & 16)
                throw std::runtime_error("valid wire peers caused negotiation failure");
            return result;
        };
        unsigned l = edge(left, a, true), r = edge(right, b, true);
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        Monitor lm, rm;
        unsigned ready = 0, completed = 0, checkpoint = 0, progress = 25000000;
        for (unsigned cycle = 0; cycle < 350000000; ++cycle) {
            const unsigned old_l = l, old_r = r;
            // The right reference edge follows the left edge. Each receiver
            // samples the other peer's currently achieved digital pulse level.
            a.pulse = (r & 1) != 0;
            l = edge(left, a, false);
            b.pulse = (l & 1) != 0;
            r = edge(right, b, false);
            lm.sample((l & 1) != 0, cycle);
            rm.sample((r & 1) != 0, cycle);
            if (trace && (l != old_l || r != old_r || cycle == checkpoint))
                trace << cycle << ',' << l << ',' << r << '\n';
            if (cycle == checkpoint)
                checkpoint += 500000;
            if (cycle == progress) {
                std::cerr << "autoneg wire clock " << cycle << '\n';
                progress += 25000000;
            }
            if ((l & 2) && (r & 2)) {
                if (ready++ == 0) {
                    lm.check(a.config, a.seed);
                    rm.check(b.config, b.seed);
                    if (!(l & 8) || (r & 8))
                        throw std::runtime_error("wire negotiation selected wrong master/slave roles");
                }
                a.pma = true;
                b.pma = true;
                if ((l & 4) && (r & 4)) {
                    completed = cycle + 1;
                    break;
                }
            }
        }
        if (!completed)
            throw std::runtime_error("two physical-rate FLP peers failed to complete negotiation");
        if (trace)
            trace << "complete," << completed << ',' << lm.pages.size() << ',' << rm.pages.size() << '\n';
        std::cout << completed << " reference cycles; wire-monitored bidirectional FLP negotiation, independent roles and PMA handoff passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
