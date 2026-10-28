#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#if PAUSE_KIND == 0
#include "VEthernetPauseReceive.h"
using DUT = VEthernetPauseReceive;
#elif PAUSE_KIND == 1
#include "VEthernetPauseTransmit.h"
using DUT = VEthernetPauseTransmit;
#elif PAUSE_KIND == 2
#include "VEthernetPauseTimer.h"
using DUT = VEthernetPauseTimer;
#else
#include "VEthernetPauseFilter.h"
using DUT = VEthernetPauseFilter;
#endif
#else
#include "../ethernet_pause_receive.hpp"
#include "../ethernet_pause_transmit.hpp"
#include "../ethernet_pause_timer.hpp"
#include "../ethernet_pause_filter.hpp"
long _system_clock = -1;
#if PAUSE_KIND == 0
using DUT = EthernetPauseReceive;
#elif PAUSE_KIND == 1
using DUT = EthernetPauseTransmit;
#elif PAUSE_KIND == 2
using DUT = EthernetPauseTimer;
#else
using DUT = EthernetPauseFilter;
#endif
#endif

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

#include "ethernet_frame_reference.hpp"

struct Bench {
    DUT dut;
    unsigned word = 0, quanta = 0;
    bool valid = false, enable = true, start = false, active = false;
    std::uint64_t address = 0x02123456789aull;
    unsigned output = 0, event = 0, aux = 0;
    std::uint64_t hash = 1469598103934665603ull, cycles = 0;
    Bench() {
#ifndef VERILATOR
#if PAUSE_KIND == 0 || PAUSE_KIND == 3
        dut.gmii_in = _ASSIGN(word); dut.valid_in = _ASSIGN(valid);
        dut.enable_in = _ASSIGN(enable); dut.local_address_in = _ASSIGN(address);
#elif PAUSE_KIND == 1
        dut.start_in = _ASSIGN(start); dut.quanta_in = _ASSIGN(quanta);
        dut.source_address_in = _ASSIGN(address);
#else
        dut.command_in = _ASSIGN(start); dut.quanta_in = _ASSIGN(quanta);
        dut.enable_in = _ASSIGN(enable); dut.data_active_in = _ASSIGN(active);
#endif
        dut._assign();
#endif
        step(true);
    }
    void step(bool reset = false) {
#ifdef VERILATOR
        dut.clk = 0; dut.reset = reset;
#if PAUSE_KIND == 0 || PAUSE_KIND == 3
        dut.gmii_in = word; dut.valid_in = valid;
        dut.enable_in = enable; dut.local_address_in = address;
#elif PAUSE_KIND == 1
        dut.start_in = start; dut.quanta_in = quanta; dut.source_address_in = address;
#else
        dut.command_in = start; dut.quanta_in = quanta;
        dut.enable_in = enable; dut.data_active_in = active;
#endif
        dut.eval(); dut.clk = 1; dut.eval();
#define READ(name) unsigned(dut.name)
#else
        ++_system_clock; dut._work(reset); dut._strobe(); ++_system_clock;
#define READ(name) unsigned(dut.name())
#endif
#if PAUSE_KIND == 0
        output = READ(quanta_out); event = READ(command_out); aux = READ(control_out);
#elif PAUSE_KIND == 1
        output = READ(gmii_out); event = READ(done_out); aux = READ(busy_out);
#elif PAUSE_KIND == 2
        output = READ(hold_out);
#else
        output = READ(gmii_out); event = READ(command_out); aux = READ(valid_out);
#endif
#undef READ
        for (unsigned value : {output, event, aux}) {
            hash ^= value; hash *= 1099511628211ull;
        }
        ++cycles;
    }
};

int main() {
    try {
        Bench bench;
        unsigned cases = 0;
#if PAUSE_KIND == 0
        // Positive multicast/local-unicast cases, zero resume, maximum duration,
        // then corrupted FCS, RX_ER, malformed headers/length and disabled RX.
        for (unsigned test = 0; test < 20; ++test) {
            auto bytes = frame(test == 0 ? 0 : test == 1 ? 65535 : 0x1234,
                               test == 3 ? bench.address : 0x0180c2000001ull);
            bool accept = test < 4;
            if (test == 4) bytes[71] ^= 1;
            if (test == 5) bytes[40] |= 512;
            if (test == 6) bytes[0] = 0;
            if (test == 7) bytes[7] = 0;
            if (test == 8) bytes[8] ^= 1;
            if (test == 9) bytes[20] = 0;
            if (test == 10) bytes[21] = 0;
            if (test == 11) bytes[22] = 1;
            if (test == 12) bytes[23] = 2;
            if (test >= 8 && test <= 12) { bytes.resize(68); append_fcs(bytes); }
            if (test == 13) { bytes.resize(67); append_fcs(bytes); }
            if (test == 14) { bytes.resize(69); append_fcs(bytes); }
            if (test == 15) bench.enable = false;
            if (test == 16) bytes.resize(25);
            if (test == 17) bytes.resize(0);
            if (test == 18) bytes[68] ^= 1;
            if (test == 19) bytes[13] ^= 1;
            unsigned commands = 0;
            for (unsigned i = 0; i < bytes.size(); ++i) {
                bench.valid = false; bench.word = 0; bench.step();
                require(!bench.event, "command before complete frame");
                bench.valid = true; bench.word = 256 | bytes[i]; bench.step();
                require(!bench.event, "command before FCS/end");
            }
            bench.valid = true; bench.word = 0; bench.step(); commands += bench.event;
            require(commands == unsigned(accept), "invalid PAUSE acceptance/rejection");
            if (accept) require(bench.output == (test == 0 ? 0u : test == 1 ? 65535u : 0x1234u), "wrong pause quanta byte order");
            for (unsigned i = 0; i < 12; ++i) { bench.step(); require(!bench.event, "repeated PAUSE command"); }
            bench.enable = true;
            ++cases;
        }
#elif PAUSE_KIND == 1
        // Compare every emitted byte including FCS and the mandatory IFG.
        for (unsigned quanta : {0, 1, 256, 65535}) {
            auto expected = frame(quanta);
            bench.quanta = quanta; bench.start = true; bench.step();
            require(bench.aux && !bench.output && !bench.event, "transmitter failed admission");
            bench.start = false;
            for (unsigned i = 0; i < 84; ++i) {
                // Mutating the request after acceptance cannot alter the frame.
                bench.quanta = 42; bench.address = 0x020000000001ull;
                bench.step();
                require(bench.output == (i < 72 ? 256 | expected[i] : 0), "wrong PAUSE byte/FCS/IFG");
                require(bench.event == unsigned(i == 83), "wrong completion cycle");
                require(bench.aux == unsigned(i != 83), "incorrect busy interval");
            }
            bench.address = 0x02123456789aull;
            ++cases;
        }
#elif PAUSE_KIND == 2
        for (unsigned quanta : {0, 1, 2, 65535}) {
            bench.start = true; bench.quanta = quanta; bench.active = true; bench.step();
            require(bench.output == unsigned(quanta != 0), "timer did not load command");
            bench.start = false;
            for (unsigned i = 0; i < 1518; ++i) {
                bench.step(); require(bench.output == unsigned(quanta != 0), "timer consumed active-frame time");
            }
            bench.active = false;
            for (unsigned i = 0; i < quanta * 64; ++i) {
                bench.step(); require(bench.output == unsigned(i + 1 < quanta * 64), "wrong 512-bit pause duration");
            }
            ++cases;
        }
        bench.start = true; bench.quanta = 65535; bench.step();
        bench.quanta = 0; bench.step(); require(!bench.output, "zero PAUSE did not resume");
        bench.quanta = 5; bench.step(); bench.enable = false; bench.start = false; bench.step();
        require(!bench.output, "disable retained pause");
        bench.enable = true; bench.step(); require(!bench.output, "stale pause after enable");
        cases += 2;
#else
        for (unsigned variant = 0; variant < 4; ++variant) {
            bench.step(true);
            bench.enable = variant != 3;
            std::vector<unsigned> input(40, 0), expected(40, 0);
            for (unsigned packet = 0; packet < 8; ++packet) {
                auto bytes = frame(0x1234, variant == 1 ? bench.address : 0x0180c2000001ull);
                bool control = packet % 2 != 0;
                if (!control) { bytes[20] = 8; bytes[21] = 0; bytes.resize(68); append_fcs(bytes); }
                if (packet == 3) bytes[71] ^= 1;
                if (packet == 5) { bytes[23] = 2; bytes.resize(68); append_fcs(bytes); }
                for (unsigned byte : bytes) {
                    input.push_back(256 | byte);
                    expected.push_back(control && bench.enable ? 0 : 256 | byte);
                }
                input.insert(input.end(), 12, 0);
                expected.insert(expected.end(), 12, 0);
            }
            input.insert(input.end(), 64, 0); expected.insert(expected.end(), 64, 0);
            unsigned commands = 0;
            for (unsigned i = 0; i < input.size(); ++i) {
                if (variant == 2) {
                    bench.valid = false; bench.step();
                    require(!bench.aux && !bench.event, "filter consumed processing-clock bubble");
                }
                bench.valid = true; bench.word = input[i]; bench.step();
                commands += bench.event;
                require(bench.aux == unsigned(i >= 32), "filter valid delay changed");
                if (i >= 32) require(bench.output == expected[i - 32], "PAUSE leaked or adjacent data corrupted");
            }
            require(commands == (bench.enable ? 2u : 0u), "filter accepted invalid control or lost valid command");
            ++cases;
        }
#endif
        bench.step(true);
        require(!bench.output && !bench.event && !bench.aux, "reset retained control state");
        std::cout << "PASS " << PAUSE_KIND << ' ' << cases << ' ' << bench.cycles << ' ' << bench.hash << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
