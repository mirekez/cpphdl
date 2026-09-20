#ifdef USE_VERILATOR
#include "VArbiterBench.h"
#else
#include "ArbiterRoot.h"
#include "ArbiterRoot_optimized_combs.h"
long _system_clock = 0;
#endif
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

using Output = std::array<uint64_t, 8>;

class Bench {
public:
#ifdef USE_VERILATOR
    VArbiterBench model;
#else
    ArbiterRoot model;
#endif
    Bench() {
#ifndef USE_VERILATOR
        model._assign();
#endif
        // Prepare each simulator's port representation outside the timer.
        // Timing conversion in the driver would manufacture a cpphdl slowdown.
        uint32_t state = 0x6d2b79f5;
        for (auto& sample : payloads) {
            std::array<uint32_t, 36> payload;
            for (auto& word : payload) {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                word = state;
            }
            payload.back() &= 0x1fff;
#ifdef USE_VERILATOR
            sample = payload;
#else
            for (unsigned port = 0; port < 11; ++port) {
                cpphdl::logic<103> value = 0;
                for (unsigned word = 0; word < 2; ++word) {
                    const unsigned bit = port * 103 + word * 64;
                    const unsigned offset = bit % 32;
                    uint64_t chunk = uint64_t(payload[bit / 32]) >> offset;
                    if (bit / 32 + 1 < 36) chunk |= uint64_t(payload[bit / 32 + 1]) << (32 - offset);
                    if (offset && bit / 32 + 2 < 36) chunk |= uint64_t(payload[bit / 32 + 2]) << (64 - offset);
                    value.bits(word * 64 + (word == 1 ? 38 : 63), word * 64) = chunk;
                }
                sample[port] = value;
            }
#endif
        }
    }

    Output step(uint64_t cycle) {
        const bool reset_n = cycle >= 4;
        const bool flush = cycle % 256 == 0;
        const bool ready = (cycle % 7) < 5;
        // Change outstanding requests only on a flush. Payload remains stable
        // while stalled. Exercise idle, sparse and dense arbitration separately.
        const unsigned requests = cycle / 256 % 3 == 0 ? 0 :
                                  cycle / 256 % 3 == 1 ? 0x401 : 0x7ff;
        const auto& payload = payloads[cycle / 768 % payloads.size()];
#ifdef USE_VERILATOR
        model.rst_ni = reset_n;
        model.flush_i = flush;
        model.ready = ready;
        model.requests = requests;
        for (unsigned word = 0; word < 36; ++word)
            model.payload[word] = payload[word];
        model.clk_i = 0;
        model.eval();
        Output result{model.grants, model.valid, model.index, model.id,
                      model.data, model.resp, model.last, model.user};
        model.clk_i = 1;
        model.eval();
#else
        model.reset_n = reset_n;
        model.flush = flush;
        model.ready = ready;
        model.requests = requests;
        model.payload = payload;
        calc_all(model, !reset_n);
        Output result{uint64_t(model.grants), uint64_t(model.valid), uint64_t(model.index),
                      uint64_t(model.id), uint64_t(model.data), uint64_t(model.resp),
                      uint64_t(model.last), uint64_t(model.user)};
        commit_optimized_regs(model);
        ++_system_clock;
#endif
        return result;
    }
private:
#ifdef USE_VERILATOR
    std::array<std::array<uint32_t, 36>, 256> payloads;
#else
    std::array<cpphdl::array<11, cpphdl::logic<103>, true>, 256> payloads;
#endif
};

int main(int argc, char** argv) {
    if (argc != 3) return 2;
#ifdef USE_VERILATOR
    auto* vectors = std::fopen(argv[1], "wb");
#else
    auto* vectors = std::fopen(argv[1], "rb");
#endif
    if (!vectors) return 2;
    Bench check;
    for (uint64_t cycle = 0; cycle < 20000; ++cycle) {
        auto actual = check.step(cycle);
#ifdef USE_VERILATOR
        if (std::fwrite(actual.data(), sizeof(actual), 1, vectors) != 1) return 2;
#else
        Output expected;
        if (std::fread(expected.data(), sizeof(expected), 1, vectors) != 1) return 2;
        if (actual != expected) {
            for (unsigned field = 0; field < actual.size(); ++field)
                if (actual[field] != expected[field])
                    std::fprintf(stderr, "mismatch cycle=%llu field=%u expected=%llx actual=%llx\n",
                                 (unsigned long long)cycle, field,
                                 (unsigned long long)expected[field], (unsigned long long)actual[field]);
            return 1;
        }
#endif
    }
    if (std::fclose(vectors)) return 2;
    Bench bench;
    uint64_t checksum = 0;
    const auto cycles = std::strtoull(argv[2], nullptr, 10);
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t cycle = 0; cycle < cycles; ++cycle)
        for (auto value : bench.step(cycle)) checksum = checksum * 0x100000001b3ull ^ value;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("seconds=%.9f checksum=%016llx cycles=%llu checked=20000\n", seconds,
                (unsigned long long)checksum, (unsigned long long)cycles);
}
