#include "../Clocked.h"
struct NestedPacket {uint32_t value[4];};
struct OpenMPNestedPacketMethods {
    static uint32_t mix(uint32_t value) {
        uint32_t result=0;
#pragma omp simd reduction(|:result)
        for(unsigned bit=0;bit<4;++bit)result|=((value>>(bit*8))&255)<<(bit*4);
        return result;
    }
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) const {
        NestedPacket packets[2]={{{value,index,operation,value^index}},{{operation+1,value+2,index+3,value*7}}};
        uint32_t output[256]{};
#pragma omp simd
        for(unsigned p=0;p<2;++p) {
            auto packet=packets[p];
#pragma omp simd
            for(unsigned lane=0;lane<4;++lane)output[248+p*4+lane]=mix(packet.value[lane]);
        }
        uint32_t result=0;
#pragma omp simd reduction(+:result)
        for(unsigned lane=0;lane<8;++lane)result+=output[248+lane];
        return (uint64_t(index)<<32)|result;
    }
};
static uint64_t nestedPacketReference(uint32_t operation,uint32_t index,uint32_t value) {
    uint32_t words[8]={value,index,operation,value^index,operation+1,value+2,index+3,value*7};
    uint32_t sum=0;
    for(auto word:words) {
        uint32_t mixed=0;
        for(unsigned bit=0;bit<4;++bit)mixed|=((word>>(bit*8))&255)<<(bit*4);
        sum+=mixed;
    }
    return (uint64_t(index)<<32)|sum;
}
using PipelineMethods=OpenMPNestedPacketMethods;
#define PIPELINE_CUSTOM_METHODS
#define PIPELINE_REFERENCE(CANDIDATE,OPERATION,INDEX,VALUE) nestedPacketReference(OPERATION,INDEX,VALUE)
#include "Pipeline.cpp"
