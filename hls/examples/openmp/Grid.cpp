#include "../../Clocked.h"
#ifndef GRID_ROWS
#define GRID_ROWS 64
#endif
#ifndef GRID_COLS
#define GRID_COLS 64
#endif
struct PipelineMethods {
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) const {
        uint32_t sum=19;
#pragma omp parallel for collapse(2) reduction(+:sum)
        for(int row=0;row<GRID_ROWS;++row)
            for(int col=0;col<GRID_COLS;++col)
                sum+=(value+uint32_t((row+1)*(col+1)))^operation;
        return (uint64_t(index)<<32)|sum;
    }
    uint32_t expected(uint32_t operation,uint32_t,uint32_t value) const {
        uint32_t sum=19;
        for(unsigned row=0;row<GRID_ROWS;++row)for(unsigned col=0;col<GRID_COLS;++col)
            sum+=(value+(row+1)*(col+1))^operation;
        return sum;
    }
};
#include "Test.h"
