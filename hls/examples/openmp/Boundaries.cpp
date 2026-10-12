#include "../../Clocked.h"

struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t sum = value;
        uint32_t offset = operation;
        uint32_t weights[2] = {operation, value};
        constexpr int COUNT = 1;
#pragma omp parallel for reduction(+:sum)
        for (int i = 0; i < 0; ++i) sum += 0xffffffffu;
#pragma omp parallel for firstprivate(offset) reduction(+:sum)
        for (int i = 0; i < COUNT; ++i) sum += offset ^ weights[1] ^ value;
        return (uint64_t(index) << 32) | sum;
    }
    uint32_t expected(uint32_t operation, uint32_t, uint32_t value) const { return value + operation; }
};
#include "Test.h"
