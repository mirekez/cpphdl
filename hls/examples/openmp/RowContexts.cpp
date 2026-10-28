#include "../../Clocked.h"
struct PipelineMethods {
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) const {
        uint32_t cells[4][8]{};
        const uint32_t columns[4]{index, index, index, index};
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<8;++col)
            cells[row][col]=value+row*19u+col;
#pragma omp simd
        for(unsigned row=0;row<4;++row)
            cells[row][columns[row]%8]+=cells[row][7&operation];
        uint32_t total=0;
        for(unsigned row=0;row<4;++row)total+=cells[row][index&7];
        return (uint64_t(index)<<32)|total;
    }
    uint32_t expected(uint32_t operation,uint32_t index,uint32_t value) const {
        return 8u*value+228u+4u*((index&7)+(operation&7));
    }
};
#include "Test.h"
