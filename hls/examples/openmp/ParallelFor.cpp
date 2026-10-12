#include "../../Clocked.h"

// Eight independent arithmetic lanes, followed by a balanced sum tree.
struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t lanes[8]{};
        uint32_t sum = 19;
#pragma omp parallel for default(none) shared(lanes, value, operation) schedule(static, 1)
        for (int i = 0; i < 8; ++i) {
            lanes[i] = (value + uint32_t(i)) * (operation | 1u);
            if (i & 1) lanes[i] ^= 0x12345u;
        }
#pragma omp simd reduction(+:sum)
        for (int i = 0; i < 8; ++i) sum += lanes[i];
        return (uint64_t(index) << 32) | sum;
    }
    uint32_t expected(uint32_t operation, uint32_t, uint32_t value) const {
        uint32_t sum = 19;
        for (unsigned i = 0; i < 8; ++i) {
            uint32_t lane = (value + i) * (operation | 1u);
            sum += (i & 1) ? lane ^ 0x12345u : lane;
        }
        return sum;
    }
};
#include "Test.h"
