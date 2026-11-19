#include "../Clocked.h"
#include "NestedAtomic.h"

#ifndef REJECT_CASE
#define REJECT_CASE 1
#endif

struct [[clang::annotate("CPPHDL_REPLACEMENT_MODULE=MultiplyAddReplacement")]]
       [[clang::annotate("CPPHDL_REPLACEMENT_MIN_LATENCY=2")]]
       [[clang::annotate("CPPHDL_REPLACEMENT_FILE=PipelineReplacement.sv;")]]
       StatefulAtomic {
    uint32_t retained=0;
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) {
        retained+=value;
        return (uint64_t(index)<<32)|(operation*retained+index);
    }
};

struct [[clang::annotate("CPPHDL_REPLACEMENT_MODULE=MultiplyAddReplacement")]]
       [[clang::annotate("CPPHDL_REPLACEMENT_MIN_LATENCY=2")]]
       [[clang::annotate("CPPHDL_REPLACEMENT_FILE=PipelineReplacement.sv;")]]
       RecursiveAtomic {
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) const {
        if(operation==0) return (uint64_t(index)<<32)|value;
        return command(operation-1,index,value);
    }
};

struct OuterMethods {
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) const {
#if REJECT_CASE == 1
        StatefulAtomic atomic;
        return atomic.command(operation,index,value);
#elif REJECT_CASE == 2
        return RecursiveAtomic{}.command(operation,index,value);
#else
        return NestedAtomic{}.command(operation,index,value);
#endif
    }
};

cpphdl::hls::ClockedPipeline<OuterMethods,3> worker;
