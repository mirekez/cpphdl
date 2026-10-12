#include "../../Clocked.h"

// A non-power-of-two lane count exercises the unpaired reduction-tree branch.
struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t sum = 23, parity = 0x15, product = 3, mask = ~0u, flags = 0x80;
#pragma omp parallel for simd reduction(+:sum) reduction(^:parity) reduction(*:product) reduction(&:mask) reduction(|:flags)
        for (int i = 12; i >= 0; i -= 2) {
            uint32_t lane = (value + uint32_t(i)) ^ operation;
            sum += lane;
            parity ^= lane;
            product *= (lane & 3u) + 1u;
            mask &= lane;
            flags |= lane;
        }
        return (uint64_t(index) << 32) | uint32_t(sum ^ parity ^ product ^ mask ^ flags);
    }
    uint32_t expected(uint32_t operation, uint32_t, uint32_t value) const {
        uint32_t sum = 23, parity = 0x15, product = 3, mask = ~0u, flags = 0x80;
        for (int i = 12; i >= 0; i -= 2) {
            uint32_t lane = (value + uint32_t(i)) ^ operation;
            sum += lane; parity ^= lane; product *= (lane & 3u) + 1u;
            mask &= lane; flags |= lane;
        }
        return sum ^ parity ^ product ^ mask ^ flags;
    }
};
#include "Test.h"
