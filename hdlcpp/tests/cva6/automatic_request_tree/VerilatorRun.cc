#include "VRequestTreeBench.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    VRequestTreeBench model;
    auto* vectors = std::fopen(argv[1], "wb");
    if (!vectors) return 2;
    for (unsigned requests = 0; requests < (1u << REQUEST_TREE_INPUTS); ++requests) {
        model.requests = requests;
        model.eval();
        const uint16_t output = model.tree;
        if (std::fwrite(&output, sizeof(output), 1, vectors) != 1) return 2;
    }
    if (std::fclose(vectors) != 0) return 2;
    const auto iterations = std::strtoull(argv[2], nullptr, 10);
    uint32_t state = 0x6d2b79f5;
    uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t iteration = 0; iteration < iterations; ++iteration) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        model.requests = state & ((1u << REQUEST_TREE_INPUTS) - 1);
        model.eval();
        checksum = checksum * 0x100000001b3ull ^ model.tree;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("seconds=%.9f checksum=%016llx\n", seconds,
                static_cast<unsigned long long>(checksum));
}
