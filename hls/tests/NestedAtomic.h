#pragma once
#include <cstdint>

struct [[clang::annotate("CPPHDL_REPLACEMENT_MODULE=MultiplyAddReplacement")]]
       [[clang::annotate("CPPHDL_REPLACEMENT_MIN_LATENCY=2")]]
       [[clang::annotate("CPPHDL_REPLACEMENT_FILE=PipelineReplacement.sv;")]]
       NestedAtomic {
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) const {
        uint32_t sum=operation*value+index;
        return (uint64_t(index)<<32)|sum;
    }
};
