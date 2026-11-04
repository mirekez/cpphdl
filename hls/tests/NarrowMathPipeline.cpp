#include "../../lib/math/FpConverter.h"
#include "../../lib/math/Fp32Add.h"

#define PIPELINE_CUSTOM_METHODS
struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t result;
        if (operation & 1) {
            result = cpphdl_examples::math::fp32_add_narrow(value, index);
        } else if (operation & 2) {
            result = FP32E8::from_int_raw(int32_t(value));
        } else {
            result = FP16E5::convert_raw<FP32E8>(uint16_t(value), true, true);
        }
        return (uint64_t(index) << 32) | result;
    }
};

#include "Pipeline.cpp"
