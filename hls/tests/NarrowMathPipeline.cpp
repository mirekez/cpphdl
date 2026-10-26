#include "../math/FloatConvert.h"

#define PIPELINE_CUSTOM_METHODS
struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t result;
        if (operation & 1) {
            result = cpphdl::hls::math::fp32_add_narrow(value, index);
        } else if (operation & 2) {
            result = cpphdl::hls::math::fp32_from_int_narrow(int32_t(value));
        } else {
            result = cpphdl::hls::math::fp16_to_fp32_narrow(value);
        }
        return (uint64_t(index) << 32) | result;
    }
};

#include "Pipeline.cpp"
