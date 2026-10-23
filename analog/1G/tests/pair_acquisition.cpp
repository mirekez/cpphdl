#ifdef VERILATOR
#include "VReceivePairAcquirer.h"
#include <verilated.h>
using Device = VReceivePairAcquirer;
#else
#include "../pair_acquisition.hpp"
long _system_clock = -1;
using Device = ReceivePairAcquirer;
#endif
#include "pair_training.hpp"
#include <algorithm>
#include <bitset>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        require(argc <= 2, "expected optional trace destination");
        Device device;
        unsigned input = 0x80808080;
        bool enable = true, restart = false, signal = true, advance = true, remote_master = false;
#ifndef VERILATOR
        device.samples_in = _ASSIGN(input);
        device.enable_in = _ASSIGN(enable);
        device.restart_in = _ASSIGN(restart);
        device.signal_ok_in = _ASSIGN(signal);
        device.advance_in = _ASSIGN(advance);
        device.remote_master_in = _ASSIGN(remote_master);
        device._assign();
#endif
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        struct Result { unsigned word, mapping, delays, stage; bool valid, locked, ambiguous; };
        unsigned clocks = 0;
        auto edge = [&](bool reset) {
#ifdef VERILATOR
            device.clk = 0;
            device.reset = reset;
            device.samples_in = input;
            device.enable_in = enable;
            device.restart_in = restart;
            device.signal_ok_in = signal;
            device.advance_in = advance;
            device.remote_master_in = remote_master;
            device.eval();
            device.clk = 1;
            device.eval();
#define READ(name) unsigned(device.name)
#else
            ++_system_clock;
            device._work(reset);
            device._strobe();
#define READ(name) unsigned(device.name())
#endif
            Result result{READ(samples_out), READ(mapping_out), READ(delays_out), READ(state_out),
                          bool(READ(valid_out)), bool(READ(locked_out)), bool(READ(ambiguous_out))};
            if (trace)
                trace << clocks << ',' << result.word << ',' << result.mapping << ',' << result.delays << ','
                      << result.stage << ',' << result.valid << ',' << result.locked << ',' << result.ambiguous << '\n';
            ++clocks;
            return result;
#undef READ
        };
        edge(true);
        for (unsigned i = 0; i < 1536; ++i) {
            input = i < 512 ? 0x80808080 : i < 1024 ? 0xC0C0C0C0 : 0xA0A0A0A0;
            const auto result = edge(false);
            require(!result.locked && !result.valid, "quiet, constant or non-training input falsely acquired");
        }
        std::array<unsigned, 4> physical_to_logical{{0, 1, 2, 3}};
        std::bitset<16> polarity_coverage;
        std::bitset<9> delay_coverage;
        unsigned cases = 0, checked_data = 0;
        do {
            const auto& stream = pair_training[cases % pair_training.size()];
            std::array<unsigned, 4> logical_to_physical{}, channel_delay{};
            for (unsigned p = 0; p < 4; ++p) {
                logical_to_physical[physical_to_logical[p]] = p;
                channel_delay[p] = (cases * 3 + p * 5) % 9;
                delay_coverage.set(channel_delay[p]);
            }
            const unsigned signs = cases & 15;
            const unsigned offset = (cases * 7) % 8;
            polarity_coverage.set(signs);
            remote_master = stream.master;
            input = 0x80808080;
            advance = true;
            edge(true);
            std::vector<unsigned> source;
            unsigned data_start = 10000, delivered = 0, stalls = 0;
            bool acquired = false;
            for (unsigned n = 0; n < 5000; ++n) {
                unsigned word = 0;
                const unsigned training = stream.words[(n + offset) % stream.words.size()];
                for (unsigned logical = 0; logical < 4; ++logical) {
                    const int level = n < data_start ? int((training >> (3 * logical)) & 7) - 2
                                                    : int((n * 13 + logical * 7) % 5) - 2;
                    const int noise = int((n + logical * 3) % 7) - 3;
                    word |= unsigned(128 + 32 * level + noise) << (8 * logical);
                }
                source.push_back(word);
                input = 0;
                for (unsigned p = 0; p < 4; ++p) {
                    const unsigned prior = n < channel_delay[p] ? 0x80808080 : source[n - channel_delay[p]];
                    unsigned value = (prior >> (8 * physical_to_logical[p])) & 255;
                    if (signs & (1 << p))
                        value = 256 - value;
                    input |= value << (8 * p);
                }
                // A negotiated role is captured at startup, not followed live.
                remote_master = n < 512 ? stream.master : !stream.master;
                auto result = edge(false);
                if (result.locked && !acquired) {
                    require(n >= 160, "pair lock bypassed PRBS qualification");
                    acquired = true;
                    data_start = n + 64;
                }
                if (result.locked) {
                    unsigned map = 0, taps = 0;
                    const unsigned a_delay = channel_delay[logical_to_physical[0]];
                    for (unsigned logical = 0; logical < 4; ++logical) {
                        const unsigned physical = logical_to_physical[logical];
                        map |= physical << (2 * logical);
                        taps |= (8 + a_delay - channel_delay[physical]) << (5 * logical);
                    }
                    require(result.mapping == map && result.delays == taps,
                            "acquired pair mapping/skew differs in permutation " + std::to_string(cases));
                    if (result.valid) {
                        require(n >= 9 + a_delay, "aligned output preceded buffered samples");
                        const unsigned time = n - 9 - a_delay;
                        unsigned expected = 0;
                        for (unsigned logical = 0; logical < 4; ++logical) {
                            unsigned value = (source[time] >> (8 * logical)) & 255;
                            if (signs & (1 << logical_to_physical[logical]))
                                value = 256 - value;
                            expected |= value << (8 * logical);
                        }
                        require(result.word == expected, "deskew altered a sample or symbol timestamp");
                        if (time >= data_start) {
                            ++delivered;
                            ++checked_data;
                        }
                    }
                } else {
                    require(!acquired && !result.valid, "pair lock was lost during ordinary data");
                }
                if (acquired && n > data_start && n % 67 == 0) {
                    advance = false;
                    const auto held = edge(false);
                    require(held.locked && !held.valid && held.word == result.word,
                            "missing advance shifted buffered samples or asserted a transfer");
                    advance = true;
                    ++stalls;
                }
                if (delivered >= 512)
                    break;
            }
            require(acquired && delivered == 512 && stalls > 0, "pair acquisition timed out before checked data");
            signal = false;
            require(!edge(false).locked, "signal loss retained pair lock");
            signal = true;
            restart = true;
            require(!edge(false).valid, "restart retained an aligned transfer");
            restart = false;
            enable = false;
            require(!edge(false).locked, "disable retained pair lock");
            enable = true;
            ++cases;
        } while (std::next_permutation(physical_to_logical.begin(), physical_to_logical.end()));
        require(cases == 24 && polarity_coverage.all() && delay_coverage.all(), "pair/phase coverage is incomplete");
        std::cout << "PASS " << clocks << " clocks; all 24 cable permutations, 16 polarity masks, skew 0..8 symbols, "
                  << checked_data << " exact data quartets, advance gaps and lock withdrawal\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
