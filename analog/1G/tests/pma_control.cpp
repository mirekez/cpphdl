#ifdef VERILATOR
#include "VPMAControl.h"
#include <verilated.h>
using Device = VPMAControl;
#else
#include "../pma_control.hpp"
long _system_clock = -1;
using Device = PMAControl;
#endif
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        Device device;
        bool enable = true, restart = false, master = false, scrambler = false;
        bool local = false, remote = false, active = false, tick = false;
#ifndef VERILATOR
        device.enable_in = _ASSIGN(enable);
        device.restart_in = _ASSIGN(restart);
        device.master_in = _ASSIGN(master);
        device.scrambler_ok_in = _ASSIGN(scrambler);
        device.local_receiver_ok_in = _ASSIGN(local);
        device.remote_receiver_ok_in = _ASSIGN(remote);
        device.transmit_active_in = _ASSIGN(active);
        device.tick_ms_in = _ASSIGN(tick);
        device._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        unsigned cycles = 0, observed = 0;
        auto edge = [&](unsigned state, unsigned mode, bool link, bool timeout, bool role, bool reset = false) {
#ifdef VERILATOR
            device.clk = 0;
            device.reset = reset;
            device.enable_in = enable;
            device.restart_in = restart;
            device.master_in = master;
            device.scrambler_ok_in = scrambler;
            device.local_receiver_ok_in = local;
            device.remote_receiver_ok_in = remote;
            device.transmit_active_in = active;
            device.tick_ms_in = tick;
            device.eval();
            device.clk = 1;
            device.eval();
            observed = unsigned(device.state_out) | (unsigned(device.mode_out) << 3) |
                       (unsigned(device.link_out) << 5) | (unsigned(device.timeout_out) << 6) |
                       (unsigned(device.master_out) << 7);
#else
            ++_system_clock;
            device._work(reset);
            device._strobe();
            observed = unsigned(device.state_out()) | (unsigned(device.mode_out()) << 3) |
                       (unsigned(device.link_out()) << 5) | (unsigned(device.timeout_out()) << 6) |
                       (unsigned(device.master_out()) << 7);
#endif
            const unsigned expected = state | (mode << 3) | (unsigned(link) << 5) |
                                      (unsigned(timeout) << 6) | (unsigned(role) << 7);
            if (trace)
                trace << cycles << ',' << observed << '\n';
            if (observed != expected)
                throw std::runtime_error("PMA state/mode/link/role contract at clock " + std::to_string(cycles) +
                                         ": got " + std::to_string(observed) + ", expected " + std::to_string(expected));
            ++cycles;
        };
        for (unsigned role = 0; role < 2; ++role) {
            master = role;
            scrambler = local = remote = active = tick = false;
            edge(0, 0, false, false, false, true);
            edge(1, 0, false, false, role);
            // A slave remains silent even if unqualified receiver flags assert.
            if (!role) {
                local = remote = true;
                for (unsigned i = 0; i < 200; ++i)
                    edge(1, 0, false, false, role);
                local = remote = false;
                scrambler = true;
            }
            edge(2, 1, false, false, role);
            // An input role change cannot change clock ownership mid-attempt.
            master = !role;
            local = remote = true;
            for (unsigned i = 1; i < 125; ++i)
                edge(2, 1, false, false, role);
            edge(4, 2, true, false, role);
            // The dwell timer blocks early mode changes; link withdraws now.
            remote = false;
            for (unsigned i = 1; i < 125; ++i)
                edge(4, 2, false, false, role);
            edge(3, 1, false, false, role);
            remote = true;
            for (unsigned i = 1; i < 125; ++i)
                edge(3, 1, false, false, role);
            edge(4, 2, true, false, role);
            local = false;
            active = true;
            for (unsigned i = 0; i < 160; ++i)
                edge(4, 2, false, false, role);
            active = false;
            edge(1, 0, false, false, role);
            // Silent-to-training and local-only reception select idle, not data.
            scrambler = true;
            edge(2, 1, false, false, role);
            local = true;
            remote = false;
            for (unsigned i = 1; i < 125; ++i)
                edge(2, 1, false, false, role);
            edge(3, 1, false, false, role);
            local = false;
            for (unsigned i = 1; i < 125; ++i)
                edge(3, 1, false, false, role);
            edge(1, 0, false, false, role);
            enable = false;
            edge(0, 0, false, false, false);
            enable = true;
            master = role;
            scrambler = false;
            edge(1, 0, false, false, role);
            if (role)
                edge(2, 1, false, false, role);
            // Inject millisecond enables with holes: only asserted ticks count.
            const unsigned limit = role ? 750 : 350;
            for (unsigned ms = 1; ms <= limit; ++ms) {
                tick = false;
                for (unsigned hole = 0; hole < 3; ++hole)
                    edge(role ? 2 : 1, role ? 1 : 0, false, false, role);
                tick = true;
                edge(ms == limit ? 5 : role ? 2 : 1, ms == limit ? 0 : role ? 1 : 0,
                     false, ms == limit, role);
            }
            tick = false;
            scrambler = local = remote = true;
            for (unsigned i = 0; i < 200; ++i)
                edge(5, 0, false, true, role);
            restart = true;
            edge(0, 0, false, false, false);
            restart = false;
            edge(1, 0, false, false, role);
            edge(2, 1, false, false, role);
            enable = false;
            edge(0, 0, false, false, false);
            enable = true;
        }
        std::cout << cycles << " PMA observations; both roles, quiet slave, 1 us dwell, receiver handshake, "
                     "frame completion, bounded acquisition, restart and disable passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
