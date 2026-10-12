#include "../Clocked.h"
static constexpr uint32_t global_seed = 17;
struct OpenMPRejectedMethods {
    static uint32_t helper(uint32_t value) { return value * 3u; }
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
        for (int i = 0; i < 257; ++i) sum += value;
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
            for (int j = 0; j < 2; ++j) sum += value;
        }
#elif OPENMP_REJECT == 13
#pragma omp parallel for collapse(2) reduction(+:sum)
        for (int i = 0; i < 20; ++i)
            for (int j = 0; j < 20; ++j) sum += value;
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
