#include "../Clocked.h"
#include "NestedAtomic.h"
#ifndef MULTI_READ_CASE
#define MULTI_READ_CASE 2
#endif
#ifndef SOURCE_READ_DEPTH
#define SOURCE_READ_DEPTH 8
#endif
#if MULTI_READ_CASE==6
#include "../math/Fp32MacPipeline.h"
#endif
struct AddressPair {uint32_t lane[2];};
AddressPair source_addresses(uint32_t seed,uint32_t bias) {
    AddressPair result{};
#pragma omp simd
    for(unsigned i=0;i<2;++i)result.lane[i]=uint32_t(NestedAtomic{}.command(seed+i,bias,3));
    return result;
}
struct MultiReadMethods {
    uint64_t command(uint32_t base,uint32_t index,uint32_t value) const {
#if MULTI_READ_CASE==6
        auto memory=cpphdl::hls::external_memory<float>(base);
#else
        auto memory=cpphdl::hls::external_memory<uint32_t>(base);
#endif
#if MULTI_READ_CASE==0
        uint32_t a=memory[index];uint32_t b=memory[index+1];
        return (uint64_t(a)<<32)|b;
#elif MULTI_READ_CASE==1
        return value&1?memory[index]:value+7;
#elif MULTI_READ_CASE==2
        uint32_t first=memory[index];
        auto pair=source_addresses(first,value);
        uint32_t second=0;
        if(value&1)second=memory[32+(pair.lane[0]&15)];
        uint32_t third=memory[64+(second&7)];
        return (uint64_t(pair.lane[1]+third)<<32)|(second+first);
#elif MULTI_READ_CASE==3
        return uint64_t(index)*value+base;
#elif MULTI_READ_CASE==4
        uint32_t discarded=memory[index];
        return value+7;
#elif MULTI_READ_CASE==6
        float a=memory[index],b=memory[index+1];
        float result=cpphdl::hls::math::Fp32MacMethods::calculate(0.5f,a,b);
        return (uint64_t(cpphdl::hls::math::encode(a))<<32)|cpphdl::hls::math::encode(result);
#else
        uint32_t a=memory[index];uint32_t b=memory[index];
        return (uint64_t(a)<<32)|b;
#endif
    }
};
cpphdl::hls::ClockedMemory<MultiReadMethods,32,false,64,SOURCE_READ_DEPTH,true> worker;
#ifndef SYNTHESIS
#include <cstdio>
int main() {
    uint32_t words[128];for(unsigned i=0;i<128;++i) {
#if MULTI_READ_CASE==6
        words[i]=0x3d000000u|(((0x4000u+i)*0x9e3779b9u)&0x01ffffffu)|((i&1)?0x80000000u:0u);
#else
        words[i]=i*13+7;
#endif
    }
    cpphdl::hls::bind_external_memory(words,sizeof(words),0x10000);
    for(unsigned index=0;index<8;++index)for(unsigned value=0;value<8;++value) {
        uint64_t expected;
#if MULTI_READ_CASE==0
        expected=(uint64_t(words[index])<<32)|words[index+1];
#elif MULTI_READ_CASE==1
        expected=value&1?words[index]:value+7;
#elif MULTI_READ_CASE==2
        uint32_t first=words[index],second=value&1?words[32+((first*3+value)&15)]:0;
        expected=(uint64_t((first+1)*3+value+words[64+(second&7)])<<32)|(second+first);
#elif MULTI_READ_CASE==3
        expected=uint64_t(index)*value+0x10000;
#elif MULTI_READ_CASE==4
        expected=value+7;
#elif MULTI_READ_CASE==6
        float a=cpphdl::hls::math::decode(words[index]),b=cpphdl::hls::math::decode(words[index+1]);
        volatile float product=a*b;
        expected=(uint64_t(words[index])<<32)|cpphdl::hls::math::encode(0.5f+product);
#else
        expected=(uint64_t(words[index])<<32)|words[index];
#endif
        if(MultiReadMethods{}.command(0x10000,index,value)!=expected)return 1;
    }
    std::puts("PASS native multiple source reads");
}
#endif
