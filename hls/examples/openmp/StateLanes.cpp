#include "../../Clocked.h"

struct PipelineMethods {
    uint32_t history[8]{};
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        uint32_t sum = 0;
#pragma omp parallel for reduction(+:sum)
        for (int i = 0; i < 8; ++i) {
            uint32_t previous = history[i];
            history[i] = value + uint32_t(i);
            sum += (previous + history[i]) ^ operation;
        }
        return (uint64_t(index) << 32) | sum;
    }
    uint32_t expected(uint32_t operation, uint32_t, uint32_t value) const {
        uint32_t sum = 0;
        for (unsigned i = 0; i < 8; ++i) sum += (history[i] + value + i) ^ operation;
        return sum;
    }
};
#include "Test.h"
