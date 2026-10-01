#include "WordGraph.h"
#include "model.h"
#include <cstdio>
#include <cstdlib>

long _system_clock = 0;

int main()
{
    WordGraph original{};
    cxxrtl_design::p_WordGraph lowered;
    cpphdl::array<16, cpphdl::logic<64>, true> payload{};
    cpphdl::logic<4> select{};
    cpphdl::logic<1> advance{};
    original.payload_in = _ASSIGN(payload);
    original.select_in = _ASSIGN(select);
    original.advance_in = _ASSIGN(advance);
    original._assign();
    uint64_t random = 0x9e3779b97f4a7c15;
    uint64_t expected_history = 0;
    for (unsigned cycle = 0; cycle < 20000; ++cycle) {
        uint64_t words[16];
        for (unsigned lane = 0; lane < 16; ++lane) {
            random ^= random << 13;
            random ^= random >> 7;
            random ^= random << 17;
            words[lane] = random;
            payload[lane] = random;
            lowered.p_payload__in.data[lane * 2] = static_cast<uint32_t>(random);
            lowered.p_payload__in.data[lane * 2 + 1] = static_cast<uint32_t>(random >> 32);
        }
        const bool reset = cycle % 127 == 0;
        select = cycle % 16;
        advance = cycle % 3 != 0;
        lowered.p_select__in.set<uint32_t>(cycle % 16);
        lowered.p_advance__in.set<bool>(cycle % 3 != 0);
        lowered.p_reset.set<bool>(reset);
        lowered.p_clk.set<bool>(false);
        lowered.step();
        if (uint64_t(original.selected_out()) != words[cycle % 16] ||
            lowered.p_selected__out.get<uint64_t>() != words[cycle % 16] ||
            uint64_t(original.history_out()) != expected_history ||
            lowered.p_history__out.get<uint64_t>() != expected_history) {
            std::fprintf(stderr, "netlist mismatch before edge at cycle %u\n", cycle);
            return 1;
        }
        original._work(reset);
        original._strobe();
        ++_system_clock;
        lowered.p_clk.set<bool>(true);
        lowered.step();
        // Refresh combinational output aliases after committing the registers;
        // the second evaluation has no new clock edge.
        lowered.step();
        if (reset) expected_history = 0;
        else if (cycle % 3 != 0) expected_history ^= words[cycle % 16];
        if (uint64_t(original.history_out()) != expected_history ||
            lowered.p_history__out.get<uint64_t>() != expected_history) {
            std::fprintf(stderr, "netlist mismatch after edge at cycle %u: cpp=%llx net=%llx expected=%llx\n", cycle,
                (unsigned long long)uint64_t(original.history_out()),
                (unsigned long long)lowered.p_history__out.get<uint64_t>(),
                (unsigned long long)expected_history);
            return 1;
        }
    }
    std::puts("netlist: 20000 cycles, all outputs and register transitions match");
}
