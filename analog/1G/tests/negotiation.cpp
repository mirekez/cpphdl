#ifdef VERILATOR
#include "VAutonegotiator.h"
#include <verilated.h>
#else
#include "../negotiation.hpp"
long _system_clock = -1;
#endif
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
#ifdef VERILATOR
        VAutonegotiator dut;
#else
        Autonegotiator dut;
#endif
        unsigned received = 0, local = 12, entropy = 0x317;
        bool receive_valid = false, receive_fault = false, burst = false, ms = false;
        bool enable = true, restart = false, link = false;
#ifndef VERILATOR
        dut.receive_page_in = _ASSIGN(received);
        dut.receive_valid_in = _ASSIGN(receive_valid);
        dut.receive_fault_in = _ASSIGN(receive_fault);
        dut.transmit_burst_in = _ASSIGN(burst);
        dut.tick_ms_in = _ASSIGN(ms);
        dut.enable_in = _ASSIGN(enable);
        dut.restart_in = _ASSIGN(restart);
        dut.pma_link_in = _ASSIGN(link);
        dut.local_up1_in = _ASSIGN(local);
        dut.entropy_seed_in = _ASSIGN(entropy);
        dut._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        unsigned observations = 0, page = 0, flags = 0;
        bool pause = false;
        auto require = [](bool condition, const char* message) {
            if (!condition)
                throw std::runtime_error(message);
        };
        auto tick = [&](bool reset = false) {
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.receive_page_in = received;
            dut.receive_valid_in = receive_valid;
            dut.receive_fault_in = receive_fault;
            dut.transmit_burst_in = burst;
            dut.tick_ms_in = ms;
            dut.enable_in = enable;
            dut.restart_in = restart;
            dut.pma_link_in = link;
            dut.local_up1_in = local;
            dut.entropy_seed_in = entropy;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            page = dut.transmit_page_out;
            pause = dut.pause_out;
            flags = unsigned(dut.transmit_enable_out) | (unsigned(dut.pma_enable_out) << 1) |
                    (unsigned(dut.link_out) << 2) | (unsigned(dut.master_out) << 3) |
                    (unsigned(dut.fault_out) << 4) | (unsigned(dut.retry_out) << 5) |
                    (unsigned(dut.reason_out) << 6);
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            page = dut.transmit_page_out();
            pause = dut.pause_out();
            flags = unsigned(dut.transmit_enable_out()) | (unsigned(dut.pma_enable_out()) << 1) |
                    (unsigned(dut.link_out()) << 2) | (unsigned(dut.master_out()) << 3) |
                    (unsigned(dut.fault_out()) << 4) | (unsigned(dut.retry_out()) << 5) |
                    (unsigned(dut.reason_out()) << 6);
#endif
            require(!(flags & 4) || (flags & 2), "link without PMA enable");
            require(!(flags & 2) || !(flags & 17), "PMA and FLP/fault simultaneously active");
            require(!(flags & 8) || (flags & 2), "clock role exposed before negotiation completes");
            require(!(flags & 16) || !(flags & 7), "configuration fault enabled a link");
            if (trace)
                trace << observations << ',' << page << ',' << flags << ',' << pause << '\n';
            ++observations;
        };
        auto reset_case = [&]() {
            receive_valid = false;
            receive_fault = false;
            burst = false;
            ms = false;
            restart = false;
            enable = true;
            link = false;
            tick(true);
            require((flags & 63) == 0, "reset did not isolate the PHY");
        };
        auto begin_attempt = [&]() {
            receive_valid = false;
            burst = false;
            ms = false;
            for (unsigned i = 0; i < 7; ++i)
                tick(); // Ordinary clocks do not replace the millisecond enable.
            require(!(flags & 1), "quiet interval counted non-millisecond clocks");
            ms = true;
            for (unsigned i = 0; i < 1299; ++i)
                tick();
            require(!(flags & 1), "break-link interval ended early");
            tick();
            ms = false;
            require((flags & 3) == 1 && page == 0x8401, "base-page advertisement did not start");
        };
        auto receive = [&](unsigned value) {
            received = value;
            receive_valid = true;
            tick();
            receive_valid = false;
        };
        auto qualify = [&](unsigned remote_page, unsigned advertised) {
            require(page == advertised, "wrong page order, toggle or captured local fields");
            receive(remote_page);
            receive(remote_page);
            require((page & 0x4000) == 0, "ACK before three matching abilities");
            for (unsigned i = 0; i < 5; ++i)
                tick();
            require(page == advertised, "an idle cycle changed the advertisement");
            receive(remote_page);
            require(page == (advertised | 0x4000), "three matching abilities did not assert ACK");
            receive(remote_page | 0x4000);
            receive(remote_page | 0x4000);
            // Completed bursts before the third ACK must not satisfy the
            // COMPLETE ACKNOWLEDGE count.
            burst = true;
            for (unsigned i = 0; i < 9; ++i)
                tick();
            burst = false;
            require(page == (advertised | 0x4000), "incomplete ACK advanced the page");
            receive(remote_page | 0x4000);
        };
        auto complete = [&](unsigned advertised, bool later_page = false) {
            receive_valid = later_page;
            burst = true;
            for (unsigned i = 0; i < 6; ++i) {
                tick();
                require(page == (advertised | 0x4000), "fewer than six complete-ACK bursts");
            }
            tick();
            burst = false;
        };
        auto base = [&]() { qualify(0x8001, 0x8401); complete(0x8401); };
        auto messages = [&](unsigned remote_config, unsigned captured_local) {
            qualify(0xB808, 0xB808);
            complete(0xB808);
            qualify(0x9000 | remote_config, 0x9000 | captured_local);
            complete(0x9000 | captured_local);
        };

        // Capture local fields; require the full ordered message exchange.
        reset_case();
        begin_attempt();
        local = 0;
        entropy = 0;
        base();
        // An old toggle, even repeated, must not acknowledge the next page.
        for (unsigned i = 0; i < 9; ++i)
            receive(0xB008);
        require(page == 0xB808, "old next-page toggle advanced arbitration");
        messages(8, 12);
        qualify(0x1DAA, 0x1B17);
        complete(0x1B17);
        require((flags & 15) == 10, "multiport role/PMA qualification wrong");
        link = true;
        tick();
        require((flags & 15) == 14, "PMA link did not qualify forwarding");
        link = false;
        tick();
        require((flags & 63) == 32 && (flags >> 6) == 5, "link loss did not restart quietly");

        // The matched NP bit remains authoritative during complete ACK.
        local = 9; // Manual slave.
        entropy = 33;
        reset_case();
        begin_attempt();
        base();
        messages(11, 9); // Remote manual master.
        qualify(0x9807, 0x1821); // Remote still has pages; local UP2 is last.
        received = 0x5807; // Later NP=0 must not overwrite matched NP=1.
        receive_valid = true;
        complete(0x1821, true);
        receive_valid = false;
        require((flags & 3) == 1 && page == 0x3001, "live NP bit prematurely completed negotiation");
        qualify(0x3001, 0x3001);
        complete(0x3001);
        require((flags & 15) == 2, "manual slave or null-page completion wrong");
        ms = true;
        for (unsigned i = 0; i < 849; ++i)
            tick();
        require((flags & 2) != 0, "PMA inhibit timer ended early");
        tick();
        ms = false;
        require((flags & 63) == 32 && (flags >> 6) == 5, "PMA training timeout did not restart");

        // Inconsistent ACK words cannot replace the accepted ability.
        local = 12;
        reset_case();
        begin_attempt();
        for (unsigned i = 0; i < 3; ++i)
            receive(0x8001);
        for (unsigned i = 0; i < 3; ++i)
            receive(0xC401);
        require((flags & 63) == 32 && (flags >> 6) == 3, "inconsistent ACK did not restart");

        // A peer may acknowledge from its first page. The same three observed
        // pages then satisfy ability and ACK qualification together.
        reset_case();
        begin_attempt();
        for (unsigned i = 0; i < 3; ++i)
            receive(0xC001);
        tick();
        complete(0x8401);
        require(page == 0xB808, "already-acknowledged abilities required duplicate ACK qualification");

        // Unsupported message codes refuse a 1G link and permit a later retry.
        reset_case();
        begin_attempt();
        base();
        qualify(0xB809, 0xB808);
        require((flags & 63) == 32 && (flags >> 6) == 4, "unsupported message established a link");

        local = 11;
        reset_case();
        begin_attempt();
        base();
        messages(11, 11);
        qualify(0x1807, 0x1821);
        for (unsigned i = 0; i < 3; ++i)
            tick();
        require((flags & 16) && (flags >> 6) == 1, "conflicting manual masters did not fault");

        // Equal seeds request new entropy; the seventh consecutive collision
        // terminates attempts with a configuration fault.
        local = 12;
        entropy = 77;
        reset_case();
        for (unsigned attempt = 0; attempt < 7; ++attempt) {
            begin_attempt();
            base();
            messages(12, 12);
            qualify(0x184D, 0x184D);
            for (unsigned i = 0; i < 3; ++i)
                tick();
            require((flags >> 6) == 2 && (flags & 32), "equal seed did not request a retry");
            require(bool(flags & 16) == (attempt == 6), "wrong consecutive collision limit");
        }
        for (unsigned i = 0; i < 1500; ++i)
            tick();
        require((flags & 17) == 16, "collision limit spontaneously enabled FLPs");
        restart = true;
        tick();
        restart = false;
        begin_attempt();
        require(!(flags & 16), "explicit restart did not clear the collision fault");

        // A receive fault breaks an otherwise consecutive ability run.
        receive(0x8001);
        receive(0x8001);
        receive_fault = true;
        tick();
        receive_fault = false;
        receive(0x8001);
        require((page & 0x4000) == 0, "invalid FLP did not break ability consecutiveness");
        // Timer strobe, not wall-clock assumptions, drives the unit timeout.
        ms = true;
        for (unsigned i = 0; i < 1400; ++i)
            tick();
        ms = false;
        require((flags & 63) == 32 && (flags >> 6) == 6, "missing-page timeout did not restart");
        enable = false;
        tick();
        require((flags & 63) == 0, "disable did not isolate negotiation and PMA");
        // Our symmetric-only advertisement resolves PAUSE only when the peer
        // advertises its symmetric bit. Asymmetric-only is insufficient.
        for (unsigned ability : {0u, 0x0400u, 0x0800u, 0x0c00u}) {
            local = 9; entropy = 33;
            reset_case(); begin_attempt();
            qualify(0x8001 | ability, 0x8401); complete(0x8401);
            messages(11, 9);
            qualify(0x1807, 0x1821); complete(0x1821);
            require(!pause, "PAUSE enabled before PMA link qualification");
            link = true; tick();
            require((flags & 4) && pause == ((ability & 0x0400) != 0), "wrong symmetric PAUSE resolution");
            link = false; tick();
            require(!pause, "link loss retained PAUSE negotiation");
        }
        std::cout << observations << " page-arbitration observations; ordered pages, ACK/toggle/NP races, roles, timers and seven collisions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
