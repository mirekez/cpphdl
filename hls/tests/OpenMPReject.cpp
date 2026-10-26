#include "../Clocked.h"
#include "../math/Float.h"
static constexpr uint32_t global_seed = 17;
static uint32_t mutable_seed = 1;
struct OpenMPRejectedMethods {
    static uint32_t aliased(const uint32_t& x) { return x; }
    static uint32_t recursive(uint32_t x) { return x?recursive(x-1):0; }
    static uint32_t helper(uint32_t value) { return value * 3u + mutable_seed++; }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t sum = 0, values[8]{}, scratch = 0;
#if OPENMP_REJECT == 0
#pragma omp parallel for reduction(+:sum)
        for (unsigned i = 0; i < index; ++i) sum += value;
#elif OPENMP_REJECT == 1
#pragma omp parallel for
        for (int i = 0; i < 8; ++i) sum += value;
#elif OPENMP_REJECT == 2
#pragma omp parallel for
        for (int i = 0; i < 7; ++i) values[i] = values[i + 1] + value;
#elif OPENMP_REJECT == 3
#pragma omp parallel for reduction(+:sum) schedule(dynamic)
        for (int i = 0; i < 8; ++i) sum += value;
#elif OPENMP_REJECT == 4
#pragma omp parallel
        { scratch = value; }
#elif OPENMP_REJECT == 5
#pragma omp parallel for reduction(+:sum)
        for (int i = 0; i < 4097; ++i) sum += value;
#elif OPENMP_REJECT == 6
        int32_t signed_sum = 0;
#pragma omp parallel for reduction(+:signed_sum)
        for (int i = 0; i < 8; ++i) signed_sum += value;
        sum = signed_sum;
#elif OPENMP_REJECT == 7
#pragma omp parallel for lastprivate(scratch)
        for (int i = 0; i < 8; ++i) scratch = value;
#elif OPENMP_REJECT == 8
#pragma omp parallel for private(scratch) reduction(+:sum)
        for (int i = 0; i < 8; ++i) sum += scratch;
#elif OPENMP_REJECT == 9
#pragma omp parallel for firstprivate(scratch) reduction(+:sum)
        for (int i = 0; i < 8; ++i) { scratch += value; sum += scratch; }
#elif OPENMP_REJECT == 10
#pragma omp parallel for private(scratch) reduction(+:sum)
        for (int i = 0; i < 8; ++i) { if (operation) scratch = value; sum += scratch; }
#elif OPENMP_REJECT == 11
#pragma omp parallel for
        for (int i = 0; i < 9; ++i) values[i] = value;
#elif OPENMP_REJECT == 12
#pragma omp parallel for reduction(+:sum)
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < index; ++j) sum += value;
        }
#elif OPENMP_REJECT == 13
#pragma omp parallel for collapse(2) reduction(+:sum)
        for (int i = 0; i < 65; ++i)
            for (int j = 0; j < 64; ++j) sum += value;
#elif OPENMP_REJECT == 14
#pragma omp parallel for default(private)
        for (int i = 0; i < 8; ++i) scratch = value;
#elif OPENMP_REJECT == 15
#pragma omp parallel for schedule(static, index) reduction(+:sum)
        for (int i = 0; i < 8; ++i) sum += value;
#elif OPENMP_REJECT == 16
#pragma omp parallel for reduction(+:sum)
        for (unsigned char i = 255; i <= 255; ++i) sum += value;
#elif OPENMP_REJECT == 17
        uint32_t* alias = values;
#pragma omp parallel for
        for (int i = 0; i < 8; ++i) alias[i] = value;
#elif OPENMP_REJECT == 18
#pragma omp parallel for reduction(+:sum)
        for (int i = 0; i < 8; ++i) sum += helper(value);
#elif OPENMP_REJECT == 19
#pragma omp parallel for
        for (int i = 0; i < 8; ++i) values[i] = values[0] + value;
#elif OPENMP_REJECT == 20
#pragma omp parallel for firstprivate(global_seed) reduction(+:sum)
        for (int i = 0; i < 8; ++i) sum += global_seed;
#elif OPENMP_REJECT == 21
#pragma omp simd reduction(+:sum)
        for(int i=0;i<4;++i)sum+=aliased(value);
#elif OPENMP_REJECT == 22
        double total;
#pragma omp simd reduction(+:total)
        for(int i=0;i<4;++i)total+=1.0;
        sum=uint32_t(total);
#elif OPENMP_REJECT == 23
#pragma omp simd reduction(+:sum)
        for(int i=0;i<4;++i)sum+=recursive(value);
#elif OPENMP_REJECT == 24
#pragma omp simd
        for(int i=0;i<4;++i)if(value)values[i-1]=value;
#elif OPENMP_REJECT == 25
#pragma omp simd
        for(int i=0;i<4;++i)values[0]=value+uint32_t(i);
#elif OPENMP_REJECT == 26
        float uninitialized;
        float total=0.0f;
#pragma omp simd private(uninitialized) reduction(+:total)
        for(int i=0;i<4;++i)total+=uninitialized;
        sum=uint32_t(total);
#elif OPENMP_REJECT == 27
        uint32_t cells[4][8]{};
#pragma omp simd
        for(unsigned row=0;row<4;++row)cells[row][index&7]=cells[(row+1)%4][index&7];
#elif OPENMP_REJECT == 28
#pragma omp simd
        for(unsigned lane=0;lane<4;++lane)values[(index+lane)&7]=value;
#elif OPENMP_REJECT == 29
        uint32_t cells[4][8]{};
#pragma omp simd
        for(unsigned row=0;row<4;++row)cells[row][index&15]=value;
#elif OPENMP_REJECT == 30
        uint32_t cells[4][8]{};
#pragma omp simd
        for(unsigned row=0;row<4;++row)cells[row][int32_t(index)%8]=value;
#elif OPENMP_REJECT == 31
        uint32_t cells[4][8]{};
#pragma omp simd
        for(unsigned row=0;row<4;++row)cells[row][index]=value;
#elif OPENMP_REJECT == 32
        uint32_t cells[2][1048577];
#pragma omp simd
        for (unsigned row = 0; row < 2; ++row) cells[row][index % 1048577] = value;
#elif OPENMP_REJECT == 33
        uint32_t cells[4][8]{};
#pragma omp simd
        for (unsigned row = 0; row < 4; ++row) cells[row][index++ & 7] = value;
#elif OPENMP_REJECT == 34
        uint32_t cells[4][8]{}, columns[4]{};
#pragma omp simd
        for (unsigned row = 0; row < 4; ++row) {
            cells[row][columns[(row + 1) % 4] & 7] = value;
            columns[row] = index;
        }
#elif OPENMP_REJECT == 35
        uint32_t cells[4][8]{}, columns[4]{};
#pragma omp simd
        for (unsigned row = 0; row < 4; ++row) {
            values[row] = cells[row][columns[(row + 1) % 4] % 8];
            columns[row] = index;
        }
#endif
        return sum ^ values[0] ^ scratch;
    }
};
class OpenMPReject : public cpphdl::Module {
public:
    cpphdl::hls::ClockedPipeline<OpenMPRejectedMethods, 3> worker;
    void _assign() { worker._assign(); }
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};
