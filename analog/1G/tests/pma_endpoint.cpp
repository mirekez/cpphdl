#ifdef VERILATOR
#ifdef ALIGNED_PMA
#include "VAlignedPMAEndpoint.h"
using Device = VAlignedPMAEndpoint;
#else
#include "VPMAEndpoint.h"
using Device = VPMAEndpoint;
#endif
#include <verilated.h>
#else
#ifdef ALIGNED_PMA
#include "../pma_aligned.hpp"
using Device = AlignedPMAEndpoint;
#else
#include "../pma_endpoint.hpp"
using Device = PMAEndpoint;
#endif
long _system_clock = -1;
#endif
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Inputs {
    bool enable = true, restart = false, master = false, valid = true, quality = true, transmitting = false;
    unsigned samples = 0x80808080, seed = 1, data = 0;
};

struct Outputs {
    unsigned symbols = 1170, receive = 0, mode = 0;
    bool link = false, locked = false, timeout = false;
};

unsigned crc_step(unsigned crc, unsigned value) {
    crc ^= value;
    for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
    return crc;
}

std::vector<unsigned> frame(unsigned length, unsigned pattern) {
    std::vector<unsigned> result(8, 0x55);
    result[7] = 0xD5;
    unsigned crc = 0xFFFFFFFF;
    for (unsigned index = 0; index < length - 4; ++index) {
        const unsigned value = (index * 83 + pattern * 61) & 255;
        result.push_back(value);
        crc = crc_step(crc, value);
    }
    for (unsigned byte = 0; byte < 4; ++byte)
        result.push_back((~crc >> (8 * byte)) & 255);
    return result;
}

// The bench supplies quantized, ordered samples to the explicit PMA boundary.
// Each is derived from the peer's actual TX output, never expected GMII bytes.
unsigned digitize(unsigned symbols, unsigned polarity) {
    unsigned result = 0;
    for (unsigned lane = 0; lane < 4; ++lane) {
        const unsigned raw = (symbols >> (3 * lane)) & 7;
        if (raw > 4)
            throw std::runtime_error("transmitter produced a non-PAM5 code");
        const int level = (int(raw) - 2) * ((polarity & (1 << lane)) ? -1 : 1);
        result |= unsigned(128 + 32 * level) << (8 * lane);
    }
    return result;
}

struct Monitor {
    std::vector<std::vector<unsigned>> expected;
    std::vector<unsigned> current;
    unsigned frames = 0, octets = 0, gap = 12;
    void sample(unsigned word) {
        if (word & 512)
            throw std::runtime_error("startup/valid channel caused a receive error");
        if (word & 256) {
            if (current.empty() && gap < 12)
                throw std::runtime_error("composed PHY violated minimum GMII IFG");
            current.push_back(word & 255);
            gap = 0;
        } else {
            ++gap;
            if (!current.empty()) {
                if (frames >= expected.size() || current != expected[frames])
                    throw std::runtime_error("composed PHY frame/preamble/FCS mismatch");
                unsigned crc = 0xFFFFFFFF;
                for (unsigned index = 8; index < current.size(); ++index)
                    crc = crc_step(crc, current[index]);
                if (crc != 0xDEBB20E3)
                    throw std::runtime_error("independent receive FCS check failed");
                octets += unsigned(current.size());
                ++frames;
                current.clear();
            }
        }
    }
};

int main(int argc, char** argv) {
    try {
        Device left, right;
        Inputs a, b;
        a.master = true;
        a.seed = 0x12345678;
        b.seed = 0x1DEADBEE;
#ifndef VERILATOR
        auto bind = [](Device& device, Inputs& in) {
            device.enable_in = _ASSIGN(in.enable);
            device.restart_in = _ASSIGN(in.restart);
            device.master_in = _ASSIGN(in.master);
            device.tick_ms_in = _ASSIGN(false);
            device.samples_valid_in = _ASSIGN(in.valid);
            device.receiver_quality_in = _ASSIGN(in.quality);
            device.samples_in = _ASSIGN(in.samples);
            device.seed_in = _ASSIGN(in.seed);
            device.transmit_data_in = _ASSIGN(in.data);
            device.transmit_enable_in = _ASSIGN(in.transmitting);
            device.transmit_error_in = _ASSIGN(false);
            device._assign();
        };
        bind(left, a);
        bind(right, b);
#endif
        auto edge = [](Device& device, const Inputs& in, bool reset) {
#ifdef VERILATOR
            device.clk = 0;
            device.reset = reset;
            device.enable_in = in.enable;
            device.restart_in = in.restart;
            device.master_in = in.master;
            device.tick_ms_in = false;
            device.samples_valid_in = in.valid;
            device.receiver_quality_in = in.quality;
            device.samples_in = in.samples;
            device.seed_in = in.seed;
            device.transmit_data_in = in.data;
            device.transmit_enable_in = in.transmitting;
            device.transmit_error_in = false;
            device.eval();
            device.clk = 1;
            device.eval();
#define READ_PORT(name) unsigned(device.name)
#else
            (void)in;
            ++_system_clock;
            device._work(reset);
            device._strobe();
#define READ_PORT(name) unsigned(device.name())
#endif
            return Outputs{READ_PORT(symbols_out), READ_PORT(receive_out), READ_PORT(mode_out),
                           bool(READ_PORT(link_out)), bool(READ_PORT(locked_out)), bool(READ_PORT(timeout_out))};
#undef READ_PORT
        };
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        Outputs l = edge(left, a, true), r = edge(right, b, true);
        unsigned cycle = 0;
#ifdef ALIGNED_PMA
        std::vector<unsigned> left_history, right_history;
        const std::array<unsigned, 4> left_order{{2, 0, 3, 1}}, right_order{{3, 2, 1, 0}};
        const std::array<unsigned, 4> left_delays{{8, 0, 3, 6}}, right_delays{{1, 7, 0, 8}};
        auto cable = [](const std::vector<unsigned>& history, const auto& order, const auto& delays, unsigned signs) {
            unsigned packed = 0;
            for (unsigned physical = 0; physical < 4; ++physical) {
                unsigned value = 128;
                if (history.size() > delays[physical]) {
                    const unsigned word = history[history.size() - 1 - delays[physical]];
                    value = (word >> (8 * order[physical])) & 255;
                }
                if (signs & (1 << physical))
                    value = 256 - value;
                packed |= value << (8 * physical);
            }
            return packed;
        };
#endif
        auto clock = [&] {
#ifdef ALIGNED_PMA
            left_history.push_back(digitize(l.symbols, 0));
            right_history.push_back(digitize(r.symbols, 0));
            a.samples = cable(right_history, left_order, left_delays, 5);
            b.samples = cable(left_history, right_order, right_delays, 10);
#else
            a.samples = digitize(r.symbols, 5);
            b.samples = digitize(l.symbols, 10);
#endif
            l = edge(left, a, false);
            r = edge(right, b, false);
            if (l.timeout || r.timeout)
                throw std::runtime_error("valid PMA peers timed out");
            if (trace)
                trace << cycle << ',' << l.symbols << ',' << r.symbols << ',' << l.receive << ',' << r.receive
                      << ',' << l.mode << ',' << r.mode << ',' << l.link << ',' << r.link << '\n';
            ++cycle;
        };
        bool slave_waited = false;
        const unsigned acquisition_limit =
#ifdef ALIGNED_PMA
            12000;
#else
            2000;
#endif
        while (!(l.link && r.link) && cycle < acquisition_limit) {
            clock();
            if (!r.locked && cycle > 4) {
                if (r.mode != 0)
                    throw std::runtime_error("slave transmitted before acquiring master scrambler");
                slave_waited = true;
            }
        }
        if (!(l.link && r.link && slave_waited))
            throw std::runtime_error("symbol-connected PHY startup failed");
        for (unsigned i = 0; i < 32; ++i)
            clock();
        Monitor lm, rm;
        lm.expected = {frame(64, 1), frame(1518, 3), frame(64, 5)};
        rm.expected = {frame(64, 2), frame(1518, 4), frame(64, 6)};
        auto exchange = [&](unsigned index) {
            for (unsigned i = 0; i < lm.expected[index].size() + 96; ++i) {
                a.transmitting = i < rm.expected[index].size();
                b.transmitting = i < lm.expected[index].size();
                a.data = a.transmitting ? rm.expected[index][i] : 0;
                b.data = b.transmitting ? lm.expected[index][i] : 0;
                clock();
                lm.sample(l.receive);
                rm.sample(r.receive);
            }
            a.transmitting = b.transmitting = false;
        };
        exchange(0);
        exchange(1);
        a.quality = false;
        bool left_withdrew = false, right_withdrew = false;
        for (unsigned i = 0; i < 256; ++i) {
            clock();
            left_withdrew |= !l.link;
            right_withdrew |= !r.link;
        }
        if (!left_withdrew || !right_withdrew)
            throw std::runtime_error("receiver failure did not propagate through physical idle status");
        a.quality = true;
        for (unsigned i = 0; i < acquisition_limit && !(l.link && r.link); ++i)
            clock();
        if (!(l.link && r.link))
            throw std::runtime_error("PMA retraining failed to restore bidirectional link");
        for (unsigned i = 0; i < 32; ++i)
            clock();
        exchange(2);
        if (lm.frames != 3 || rm.frames != 3)
            throw std::runtime_error("composed PHY lost a complete frame");
        a.enable = b.enable = false;
        clock();
        if (l.link || r.link || l.mode || r.mode)
            throw std::runtime_error("disabled PHY retained transmission or link");
        std::cout << cycle << " symbol clocks; slave-silent startup, wire-carried receive readiness, "
                  << lm.frames + rm.frames << " complete 64/1518-byte frames, " << lm.octets + rm.octets
                  << " GMII octets, independent FCS, receiver failure and retraining passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
