#include "../Clocked.h"
#include "NestedAtomic.h"

// The annotated class is a plain C++ helper, not the ClockedPipeline method
// class. This is the contract exercised by end-to-end application methods.

struct PipelineMethods {
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) const {
        uint64_t first=NestedAtomic{}.command(operation,index,value);
        if(operation&1) return first+7;
        if(operation&2) {
            uint64_t second=NestedAtomic{}.command(uint32_t(first)+1,index,value);
            return first^second;
        }
        return first;
    }
};

#define PIPELINE_CUSTOM_METHODS
#include "Pipeline.cpp"
