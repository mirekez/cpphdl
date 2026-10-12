#include "../../Clocked.h"

struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t seed = value, temporary = 0x77, sum = 29;
#pragma omp parallel for private(temporary) firstprivate(seed) reduction(+:sum)
        for (int i = 1; i <= 9; i += 2) {
            // Overwrite the private scratch before reading it. Each iteration
            // overwrites seed, so the answer does not depend on thread assignment.
            seed = value + uint32_t(i);
            temporary = seed * (operation | 1u);
            sum += temporary;
        }
        // Neither a private nor a firstprivate variable writes back to the caller.
        return (uint64_t(index) << 32) | uint32_t(sum ^ seed ^ temporary);
    }
    uint32_t expected(uint32_t operation, uint32_t, uint32_t value) const {
        uint32_t sum = 29;
        for (unsigned i = 1; i <= 9; i += 2) sum += (value + i) * (operation | 1u);
        return sum ^ value ^ 0x77;
    }
};
#include "Test.h"
