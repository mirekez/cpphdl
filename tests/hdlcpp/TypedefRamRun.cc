#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#if defined(RAM_GRAPH)
#include "model.h"
#elif defined(RAM_RTL)
#include "VTypedefRam.h"
#else
#include "generated/TypedefRam.h"
long _system_clock = 0;
#endif

constexpr unsigned Depth = 1u << RAM_ADDR_BITS;
using Row = std::array<uint64_t, 2 * RAM_WORDS>;

struct Simulation {
#if defined(RAM_GRAPH)
    cpphdl_native::Model model;
#elif defined(RAM_RTL)
    VTypedefRam model;
#else
    TypedefRam<RAM_ADDR_BITS, RAM_WORDS> model;
    cpphdl::logic<1> select, write;
    cpphdl::logic<RAM_ADDR_BITS> address;
    cpphdl::array<RAM_WORDS, cpphdl::logic<128>, true> data;
    cpphdl::array<RAM_WORDS, cpphdl::logic<16>, true> enables;
    Simulation() {
        model.cs_in = _ASSIGN(select);
        model.we_in = _ASSIGN(write);
        model.addr_in = _ASSIGN(address);
        model.wdata_in = _ASSIGN(data);
        model.wbyteenable_in = _ASSIGN(enables);
        model._assign();
    }
#endif
    Row cycle(bool selected, bool writing, unsigned location, uint32_t mask, const Row& values) {
#if defined(RAM_GRAPH)
        model.cs[0] = selected; model.we[0] = writing; model.addr[0] = location;
        model.wbyteenable[0] = mask;
#elif defined(RAM_RTL)
        model.cs = selected; model.we = writing; model.addr = location;
        model.wbyteenable = mask;
#else
        select = selected; write = writing; address = location;
        for (unsigned word = 0; word < RAM_WORDS; ++word) enables[word] = mask >> (word * 16);
#endif
        for (unsigned lane = 0; lane < 4 * RAM_WORDS; ++lane) {
            const uint32_t value = values[lane / 2] >> ((lane % 2) * 32);
#if defined(RAM_GRAPH) || defined(RAM_RTL)
            model.wdata[lane] = value;
#else
            data[lane / 4].bits((lane % 4) * 32 + 31, (lane % 4) * 32) = cpphdl::logic<32>(value);
#endif
        }
#if defined(RAM_GRAPH)
        model.eval(false); model.step();
#elif defined(RAM_RTL)
        model.clk = 0; model.eval(); model.clk = 1; model.eval();
#else
        ++_system_clock; model._work(false); model._strobe(); ++_system_clock;
#endif
        Row output{};
        for (unsigned lane = 0; lane < 4 * RAM_WORDS; ++lane) {
#if defined(RAM_GRAPH) || defined(RAM_RTL)
            const uint32_t value = model.rdata[lane];
#else
            const uint32_t value = uint64_t(model.rdata_out()[lane / 4].bits((lane % 4) * 32 + 31, (lane % 4) * 32));
#endif
            output[lane / 2] |= uint64_t(value) << ((lane % 2) * 32);
        }
        return output;
    }
};

uint64_t next_random(uint64_t& random) {
    random ^= random << 13; random ^= random >> 7; random ^= random << 17;
    return random;
}

int main() {
    Simulation simulation;
    std::array<Row, Depth> memory{};
    for (unsigned address = 0; address < Depth; ++address)
        simulation.cycle(true, true, address, ~uint32_t(0), {});
    Row expected = simulation.cycle(true, false, 0, 0, {});
    if (expected != Row{}) return 1;
    uint64_t random = 0x831ba34f092713e5ull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        const auto pattern = next_random(random);
        const bool select = ((pattern >> 17) & 3) != 0, write = (pattern >> 19) & 1;
        const unsigned address = pattern & (Depth - 1);
        const uint32_t mask =
#if defined(FULL_WRITE)
            (pattern >> 24) & 1 ? ~uint32_t(0) : 0;
#else
            pattern >> 24;
#endif
        Row data{};
        for (unsigned lane = 0; lane < data.size(); ++lane) data[lane] = pattern ^ (uint64_t(lane) * 0x9e3779b97f4a7c15ull);
        if (select && !write) expected = memory[address];
        if (select && write) for (unsigned byte = 0; byte < 16 * RAM_WORDS; ++byte) if (mask & (1u << byte)) {
            const unsigned word = byte / 8, shift = (byte % 8) * 8;
            const uint64_t lane = uint64_t(255) << shift;
            memory[address][word] = (memory[address][word] & ~lane) | (data[word] & lane);
        }
        if (simulation.cycle(select, write, address, mask, data) != expected) {
            std::fprintf(stderr, "RAM mismatch sample=%u depth=%u words=%u\n", sample, Depth, RAM_WORDS);
            return 1;
        }
    }
    uint64_t checksum = 0;
    const auto started = std::chrono::steady_clock::now();
    for (unsigned sample = 0; sample < 100000; ++sample) {
        const auto pattern = next_random(random);
        Row data{};
        for (unsigned lane = 0; lane < data.size(); ++lane) data[lane] = pattern ^ (uint64_t(lane) * 0x9e3779b97f4a7c15ull);
        const auto output = simulation.cycle(((pattern >> 17) & 3) != 0, (pattern >> 19) & 1,
                                            pattern & (Depth - 1), pattern >> 24, data);
        checksum = (checksum << 7) ^ (checksum >> 57);
        for (auto value : output) checksum ^= value;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("RAM_BENCH depth=%u words=%u cycles=100000 checked=4096 checksum=%016llx seconds=%.9f\n",
                Depth, RAM_WORDS, static_cast<unsigned long long>(checksum), seconds);
}
