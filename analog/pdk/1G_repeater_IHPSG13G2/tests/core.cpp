#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#ifdef VERILATOR
#include "VRepeaterCore.h"
using DUT = VRepeaterCore;
#else
#include "../repeater.hpp"
using DUT = RepeaterCore;
long _system_clock = -1;
#endif

std::vector<std::uint8_t> frame_bytes(unsigned length, unsigned seed) {
    std::vector<std::uint8_t> result(length + 8);
    for (unsigned i = 0; i < 7; ++i)
        result[i] = 0x55;
    result[7] = 0xd5;
    for (unsigned i = 0; i < length - 4; ++i)
        result[8 + i] = (seed * 37 + i * 13) & 255;
    result[8] = result[14] = 2; // Locally administered unicast MAC addresses.
    result[20] = 8;
    result[21] = 0; // EtherType 0x0800.
    std::uint32_t crc = 0xffffffff;
    for (unsigned i = 8; i < result.size() - 4; ++i) {
        crc ^= result[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    crc ^= 0xffffffff;
    for (unsigned i = 0; i < 4; ++i)
        result[result.size() - 4 + i] = (crc >> (8 * i)) & 255;
    return result;
}

int main(int argc, char** argv) {
    try {
        DUT dut;
        std::uint16_t a = 0, b = 0;
        bool link_a = true, link_b = true, enable = true;
        std::ofstream vectors;
        if (argc == 2) {
            vectors.open(argv[1]);
            if (!vectors)
                throw std::runtime_error("cannot write forwarding vectors");
            vectors << "cycle,reset,rx_a,rx_b,link_a,link_b,enable,tx_a,tx_b,active\n";
        }
#ifndef VERILATOR
        dut.rx_a_in = _ASSIGN(a);
        dut.rx_b_in = _ASSIGN(b);
        dut.link_a_in = _ASSIGN(link_a);
        dut.link_b_in = _ASSIGN(link_b);
        dut.enable_in = _ASSIGN(enable);
        dut._assign();
#endif
        unsigned cycles = 0, bytes = 0, errors = 0, resets = 0;
        auto edge = [&](bool reset) {
#ifndef VERILATOR
            ++_system_clock;
#endif
            bool expected_active = !reset && link_a && link_b && enable;
            unsigned expected_a = expected_active && (b & 256) ? b : 0;
            unsigned expected_b = expected_active && (a & 256) ? a : 0;
#ifdef VERILATOR
            dut.ref_clock = 0;
            dut.reset = reset;
            dut.rx_a_in = a;
            dut.rx_b_in = b;
            dut.link_a_in = link_a;
            dut.link_b_in = link_b;
            dut.enable_in = enable;
            dut.eval();
            dut.ref_clock = 1;
            dut.eval();
            unsigned actual_a = dut.tx_a_out, actual_b = dut.tx_b_out;
            bool active = dut.active_out;
#else
            dut._work(reset);
            dut._strobe();
            unsigned actual_a = unsigned(dut.tx_a_out()), actual_b = unsigned(dut.tx_b_out());
            bool active = dut.active_out();
#endif
            if (actual_a != expected_a || actual_b != expected_b || active != expected_active)
                throw std::runtime_error("forwarding/idle/error/reset mismatch at cycle " + std::to_string(cycles));
            if (vectors.is_open())
                vectors << cycles << ',' << reset << ',' << a << ',' << b << ',' << link_a << ',' << link_b << ','
                        << enable << ',' << actual_a << ',' << actual_b << ',' << active << '\n';
            ++cycles;
            resets += reset;
            bytes += bool(expected_a & 256) + bool(expected_b & 256);
            errors += bool(expected_a & 512) + bool(expected_b & 512);
        };
        edge(true);
        // Independent full-duplex streams include minimum-size and maximum-size
        // Ethernet wire frames (8 preamble/SFD octets plus 64 or 1518 frame octets).
        for (unsigned frame = 0; frame < 24; ++frame) {
            auto packet_a = frame_bytes(frame % 2 ? 1518 : 64, frame + 1);
            auto packet_b = frame_bytes(frame % 2 ? 1518 : 64, frame + 91);
            unsigned length = packet_a.size();
            for (unsigned i = 0; i < length + 12; ++i) {
                a = i < length ? 256 | packet_a[i] : 0;
                b = i < length ? 256 | packet_b[i] : 0;
                if (frame % 7 == 0 && i == 40)
                    a |= 512;
                if (frame % 5 == 0 && i == 41)
                    b |= 512;
                edge(false);
            }
        }
        // Exercise every possible data/valid/error word in both directions.
        for (unsigned word = 0; word < 1024; ++word) {
            a = word;
            b = word ^ 0x2a5;
            edge(false);
        }
        // An in-flight reset or either lost link must suppress both transmitters.
        for (unsigned mask = 0; mask < 8; ++mask) {
            link_a = mask & 1;
            link_b = mask & 2;
            enable = mask & 4;
            a = 0x1ab;
            b = 0x355;
            edge(false);
            edge(true);
            edge(false);
        }
        if (bytes < 30000 || errors < 500 || resets != 9)
            throw std::runtime_error("insufficient forwarding test coverage");
        std::cout << "PASS: " << cycles << " cycles, " << bytes << " forwarded octets, " << errors
                  << " propagated errors, " << resets << " resets\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
