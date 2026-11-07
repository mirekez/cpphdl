#include "../Clocked.h"
#include "../../lib/math/FpConvertBits.h"

struct PipelineMethods {
    static uint32_t mix(uint32_t value) {
        using Word = uint32_t;
        typedef Word Result;
        Result local = value ^ 0x12345678u;
        return local + 19u;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t out[2]{};
#pragma omp simd
        for (unsigned lane = 0; lane < 2; ++lane) {
            using Word = uint32_t;
            typedef Word Bits;
            Bits input = lane ? index : value;
            out[lane] = operation ? mix(input) : uint32_t(
                cpphdl_examples::math::FpFormatConversion::convert(
                    input & 65535u, 16, 5, 32, 8, true, true));
        }
        return uint64_t(out[0]) | (uint64_t(out[1]) << 32);
    }
};
#define PIPELINE_CUSTOM_METHODS
#include "Pipeline.cpp"
