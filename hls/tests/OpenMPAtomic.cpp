#include "../Clocked.h"
#include "NestedAtomic.h"

struct OpenMPAtomicMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t lanes[4]{};
#pragma omp simd
        for (unsigned lane = 0; lane < 4; ++lane)
            lanes[lane] = uint32_t(NestedAtomic{}.command(operation + lane, index, value));
        return uint64_t(lanes[0] ^ lanes[1] ^ lanes[2] ^ lanes[3]);
    }
};

using PipelineMethods = OpenMPAtomicMethods;
#define PIPELINE_CUSTOM_METHODS
#include "Pipeline.cpp"
