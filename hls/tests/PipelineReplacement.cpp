#include "../Clocked.h"
#ifndef REPLACEMENT_DISABLED
#define REPLACEMENT_ATTRIBUTES \
    [[clang::annotate("CPPHDL_REPLACEMENT_MODULE=MultiplyAddReplacement")]] \
    [[clang::annotate("CPPHDL_REPLACEMENT_MIN_LATENCY=2")]] \
    [[clang::annotate("CPPHDL_REPLACEMENT_FILE=PipelineReplacement.sv;")]]
#else
#define REPLACEMENT_ATTRIBUTES
#endif
struct REPLACEMENT_ATTRIBUTES PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t result = operation * value + index;
        return (uint64_t(index) << 32) | result;
    }
};
#define PIPELINE_CUSTOM_METHODS
#include "Pipeline.cpp"
