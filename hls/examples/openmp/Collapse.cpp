#include "../../Clocked.h"

// Each (row, column) is a distinct lane, not a multiplexed RAM transaction.
struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t matrix[2][3]{};
        uint32_t sum = 0;
#pragma omp parallel for collapse(2)
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 3; ++col)
                matrix[row][col] = (value + uint32_t(row * 3 + col)) ^ operation;
#pragma omp parallel for collapse(2) reduction(+:sum)
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 3; ++col) sum += matrix[row][col];
        return (uint64_t(index) << 32) | sum;
    }
    uint32_t expected(uint32_t operation, uint32_t, uint32_t value) const {
        uint32_t sum = 0;
        for (unsigned i = 0; i < 6; ++i) sum += (value + i) ^ operation;
        return sum;
    }
};
#include "Test.h"
