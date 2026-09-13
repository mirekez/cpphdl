#include "Root.h"
#include "RequestTreeRoot_optimized_combs.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

long _system_clock = 0;

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::vector<uint16_t> expected(1u << RequestTreeRoot::Inputs);
    auto* vectors = std::fopen(argv[1], "rb");
    if (!vectors) return 2;
    const auto count = std::fread(expected.data(), sizeof(uint16_t), expected.size(), vectors);
    const bool complete = count == expected.size() && std::fgetc(vectors) == EOF;
    std::fclose(vectors);
    if (!complete) return 2;

    RequestTreeRoot reference;
    RequestTreeRoot optimized;
    reference._assign();
    optimized._assign();
    for (unsigned requests = 0; requests < expected.size(); ++requests) {
        for (unsigned previous : {0u, (1u << RequestTreeRoot::TreeBits) - 1, requests >> 1}) {
            reference.requests = requests;
            optimized.requests = requests;
            reference.dut.req_nodes_comb = previous;
            optimized.dut.req_nodes_comb = previous;
            reference._work(false);
            calc_all(optimized, false);
            if (uint64_t(reference.observed) != expected[requests] ||
                uint64_t(optimized.observed) != expected[requests]) {
                std::fprintf(stderr, "mismatch inputs=%u requests=%u previous=%u RTL=%u hdlcpp=%llu cpphdl=%llu\n",
                             RequestTreeRoot::Inputs, requests, previous, expected[requests],
                             static_cast<unsigned long long>(uint64_t(reference.observed)),
                             static_cast<unsigned long long>(uint64_t(optimized.observed)));
                return 1;
            }
            ++_system_clock;
        }
    }

    const auto iterations = std::strtoull(argv[2], nullptr, 10);
    uint32_t state = 0x6d2b79f5;
    uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t iteration = 0; iteration < iterations; ++iteration) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        optimized.requests = state & ((1u << RequestTreeRoot::Inputs) - 1);
        calc_all(optimized, false);
        checksum = checksum * 0x100000001b3ull ^ uint64_t(optimized.observed);
        ++_system_clock;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("seconds=%.9f checksum=%016llx exhaustive=%zu\n", seconds,
                static_cast<unsigned long long>(checksum), expected.size() * 3);
}
