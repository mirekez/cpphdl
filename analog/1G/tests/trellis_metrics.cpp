#include "../pcs_trellis_metrics.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

long _system_clock = -1;

int main() {
    try {
        PCSTrellisMetricRegister dut;
        std::array<unsigned, 8> input{}, expected{};
        bool enable = false;
        unsigned random = 0x938572;
        dut.symbol_enable_in = _ASSIGN(enable);
        for (unsigned i = 0; i < 8; ++i)
            dut.metric_in[i] = _ASSIGN_I(input[i]);
        dut._assign();
        for (unsigned cycle = 0; cycle < 4096; ++cycle) {
            bool reset = cycle == 0 || cycle % 61 == 0;
            enable = cycle % 5 < 3;
            for (unsigned i = 0; i < 8; ++i) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                input[i] = cycle % 3 ? random & 262143 : (i == cycle % 8 ? 0 : 262143);
            }
            if (enable)
                expected = input;
            if (reset)
                expected.fill(0);
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            for (unsigned i = 0; i < 8; ++i)
                if (unsigned(dut.metric_out[i]()) != expected[i])
                    throw std::runtime_error("registered metric/reset/hold mismatch");
            unsigned slot = 0;
            for (unsigned parity = 0; parity < 2; ++parity)
                for (unsigned s = parity; s < 8; s += 2)
                    for (unsigned t = parity; t < 8; t += 2)
                        if (s != t) {
                            unsigned difference = (2097152 + expected[s] - expected[t]) % 2097152;
                            if (unsigned(dut.difference_out[slot++]()) != difference)
                                throw std::runtime_error("metric/difference symbol alignment mismatch");
                        }
        }
        std::cout << "4096 metric-register cycles: both directions, forced metrics, resets and poisoned pauses exact\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
