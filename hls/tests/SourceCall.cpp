#include "../Clocked.h"
#include "NestedAtomic.h"

#ifndef SOURCE_CALL_LANES
#define SOURCE_CALL_LANES 4
#endif
#ifndef SOURCE_READ_DEPTH
#define SOURCE_READ_DEPTH 8
#endif
struct SourceInput { uint32_t seed,index,value; };
struct SourceResult { uint32_t lane[SOURCE_CALL_LANES]; };

// No class annotation, hardware wrapper, explicit schedule or lane instances.
SourceResult parallel_values(SourceInput input,uint16_t bias,int16_t correction,uint32_t scale) {
    SourceResult result{};
#pragma omp simd
    for (unsigned lane=0;lane<SOURCE_CALL_LANES;++lane)
        result.lane[lane]=uint32_t(NestedAtomic{}.command(input.seed+lane,
            input.index+uint32_t(int32_t(bias)+int32_t(correction)),scale));
    return result;
}
struct SourceCallMethods {
    uint64_t command(uint32_t address,uint32_t index,uint32_t value) const {
#ifdef SOURCE_CALL_STREAMING
        uint32_t seed=address;
#else
        uint32_t seed=cpphdl::hls::external_memory<uint32_t>(address)[index];
#endif
        auto first=parallel_values({seed,index,value},5,-2,value);
        auto second=parallel_values({first.lane[0],index,value+1},9,-2,value+1);
        return (uint64_t(first.lane[SOURCE_CALL_LANES-1])<<32)|second.lane[SOURCE_CALL_LANES-1];
    }
};
#if defined(SOURCE_CALL_MEMORY_STREAMING)
cpphdl::hls::ClockedMemory<SourceCallMethods,32,false,64,SOURCE_READ_DEPTH,true> worker;
#elif defined(SOURCE_CALL_STREAMING)
cpphdl::hls::ClockedPipeline<SourceCallMethods,1> worker;
#else
cpphdl::hls::ClockedMemory<SourceCallMethods> worker;
#endif

#ifndef SYNTHESIS
#include <cstdio>
int main() {
    uint32_t words[2]={3,7};
    cpphdl::hls::bind_external_memory(words,sizeof(words),0x1000);
    // Independent scalar calculation, including the two distinct call sites.
    uint32_t first_last=(7+SOURCE_CALL_LANES-1)*5+4;
    uint32_t second_last=(7*5+4+SOURCE_CALL_LANES-1)*6+8;
#ifdef SOURCE_CALL_STREAMING
    const uint32_t address=7;
#else
    const uint32_t address=0x1000;
#endif
    if(SourceCallMethods{}.command(address,1,5)!=((uint64_t(first_last)<<32)|second_last))return 1;
    std::puts("PASS native ordinary OpenMP function calls");
}
#endif
