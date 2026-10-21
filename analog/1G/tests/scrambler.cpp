#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VPCSSideStream.h"
using DUT = VPCSSideStream;
#else
#include "../pcs.hpp"
using DUT = PCSSideStream;
long _system_clock = -1;
#endif
int main(int argc, char** argv) {
    try {
        std::ofstream trace;
        if (argc == 2) {
            trace.open(argv[1]);
            if (!trace)
                throw std::runtime_error("cannot open scrambler trace");
        }
        DUT dut;
        bool master = true, advance = false, load = false;
        unsigned long seed = 0;
#ifndef VERILATOR
        dut.master_in = _ASSIGN(master);
        dut.advance_in = _ASSIGN(advance);
        dut.load_in = _ASSIGN(load);
        dut.seed_in = _ASSIGN(seed);
        dut._assign();
#endif
        auto step = [&](bool reset, unsigned long expected, bool invalid) {
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.master_in = master;
            dut.advance_in = advance;
            dut.load_in = load;
            dut.seed_in = seed;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            unsigned long actual = dut.state_out;
            bool bad = dut.invalid_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            unsigned long actual = (unsigned long)dut.state_out();
            bool bad = dut.invalid_out();
#endif
            if (trace.is_open())
                trace << actual << ',' << bad << '\n';
            if (actual != expected || bad != invalid)
                throw std::runtime_error("side-stream polynomial/seed/hold mismatch");
        };
        step(true, 1, false);
        for (bool role : {true, false}) {
            master = role;
            seed = 0x123456789;
            load = true;
            step(false, seed, false);
            load = false;
            std::array<bool, 33> bits{};
            for (unsigned i = 0; i < 33; ++i)
                bits[i] = (seed >> i) & 1;
            for (unsigned cycle = 0; cycle < 10000; ++cycle) {
                advance = cycle % 7 != 0;
                if (advance) {
                    bool feedback = bits[32] != bits[master ? 12 : 19];
                    for (unsigned i = 32; i > 0; --i)
                        bits[i] = bits[i - 1];
                    bits[0] = feedback;
                }
                unsigned long expected = 0;
                for (unsigned i = 0; i < 33; ++i)
                    expected |= (unsigned long)bits[i] << i;
                step(false, expected, false);
            }
        }
        load = true;
        seed = 0;
        step(false, 0, true);
        load = false;
        advance = true;
        step(false, 0, true);
        step(true, 1, false);
        std::cout << "20000 independent bit-array master/slave polynomial/hold steps; seed/reset guards passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
