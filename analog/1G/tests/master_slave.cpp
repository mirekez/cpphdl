#ifdef VERILATOR
#include "VMasterSlaveResolver.h"
#include <verilated.h>
#else
#include "../master_slave.hpp"
long _system_clock = -1;
#endif
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
#ifdef VERILATOR
        VMasterSlaveResolver dut;
#else
        MasterSlaveResolver dut;
#endif
        unsigned local = 0, remote = 0, local_seed = 0, remote_seed = 0;
        bool advance = false;
#ifndef VERILATOR
        dut.local_up1_in = _ASSIGN(local);
        dut.remote_up1_in = _ASSIGN(remote);
        dut.local_seed_in = _ASSIGN(local_seed);
        dut.remote_seed_in = _ASSIGN(remote_seed);
        dut.advance_in = _ASSIGN(advance);
        dut._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        unsigned observed = 0, expected = 0;
        auto tick = [&](bool reset, unsigned resolved) {
            if (advance)
                expected = resolved;
            if (reset)
                expected = 0;
            unsigned actual;
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.local_up1_in = local;
            dut.remote_up1_in = remote;
            dut.local_seed_in = local_seed;
            dut.remote_seed_in = remote_seed;
            dut.advance_in = advance;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            actual = unsigned(dut.resolved_out) | (unsigned(dut.master_out) << 1) |
                     (unsigned(dut.fault_out) << 2) | (unsigned(dut.collision_out) << 3);
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            actual = unsigned(dut.resolved_out()) | (unsigned(dut.master_out()) << 1) |
                     (unsigned(dut.fault_out()) << 2) | (unsigned(dut.collision_out()) << 3);
#endif
            if (actual != expected)
                throw std::runtime_error("master/slave table mismatch at observation " + std::to_string(observed));
            if (trace)
                trace << actual << '\n';
            ++observed;
        };
        tick(true, 0);
        // The table oracle orders four device categories: forced slave,
        // automatic single-port, automatic multiport, forced master.
        // Equal automatic categories use the seed ordering instead.
        constexpr std::array<unsigned, 8> category{1, 0, 1, 3, 2, 0, 2, 3};
        for (unsigned a = 0; a < 8; ++a)
            for (unsigned b = 0; b < 8; ++b)
                for (unsigned seed = 0; seed < 2048; ++seed) {
                    const std::array<unsigned, 4> other{0, seed, (seed + 1) & 2047, 2047};
                    for (unsigned value : other) {
                        // Received reserved/ability bits cannot change role resolution.
                        local = a | ((seed * 73) & 2040);
                        remote = b | ((value * 37) & 2040);
                        local_seed = seed;
                        remote_seed = value;
                        const unsigned ca = category[a], cb = category[b];
                        unsigned result;
                        if (ca == cb && (ca == 0 || ca == 3))
                            result = 4;
                        else if (ca != cb)
                            result = ca > cb ? 3 : 1;
                        else
                            result = seed == value ? 8 : seed > value ? 3 : 1;
                        advance = observed % 19 != 0;
                        tick(observed % 131 == 0, result);
                    }
                }
        std::cout << observed << " independent role-table observations; all configurations and eleven-bit seed boundaries\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
