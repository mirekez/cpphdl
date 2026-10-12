#include "../../Clocked.h"
#ifndef OPENMP_LANES
#define OPENMP_LANES 16
#endif

struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t sum = operation;
#pragma omp parallel for reduction(+:sum)
        for (int i = 0; i < OPENMP_LANES; ++i)
            sum += (value ^ (uint32_t(i) * 0x102031u));
        return (uint64_t(index) << 32) | sum;
    }
    uint32_t expected(uint32_t operation, uint32_t, uint32_t value) const {
        uint32_t sum = operation;
        for (unsigned i = 0; i < OPENMP_LANES; ++i) sum += value ^ (i * 0x102031u);
        return sum;
    }
};
#include "Test.h"
