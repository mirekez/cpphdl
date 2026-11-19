#include "../Clocked.h"
#ifndef WRITE_CASE
#define WRITE_CASE 1
#endif
#ifndef SOURCE_READ_DEPTH
#define SOURCE_READ_DEPTH 8
#endif
#if WRITE_CASE==5
#include "../math/Fp32MacPipeline.h"
#endif
struct SourceWriteMethods {
    uint64_t command(uint32_t base,uint32_t index,uint32_t value) const {
        auto memory=cpphdl::hls::external_memory<uint32_t>(base);
#if WRITE_CASE==0
        memory[index]=value;return value;
#elif WRITE_CASE==1
        memory[index]=memory[index]+value;
        return memory[index];
#elif WRITE_CASE==2
        if(value&1)memory[index]=value^0xabc;
        return memory[index];
#elif WRITE_CASE==3
        memory[index]=value;
        memory[index+1]=value+1;
        uint32_t first=memory[index];
        uint32_t second=memory[index+1];
        return uint64_t(first)+second;
#elif WRITE_CASE==4
        auto half=cpphdl::hls::external_memory<uint16_t>(base);
        half[2*index+1]=uint16_t(value);
        return memory[index];
#elif WRITE_CASE==5
        auto floats=cpphdl::hls::external_memory<float>(base);
        float a=floats[index],b=floats[16+index];
        floats[index]=cpphdl::hls::math::Fp32MacMethods::calculate(a,b,cpphdl::hls::math::decode(value));
        return cpphdl::hls::math::encode(floats[index]);
#elif WRITE_CASE==6
        memory[index]=value;memory[index]=value;return value;
#else
        if(value&1)memory[index]=value;
        return value+3;
#endif
    }
};
cpphdl::hls::ClockedMemory<SourceWriteMethods,32,false,64,SOURCE_READ_DEPTH,true> worker;
#ifndef SYNTHESIS
#include <cstdio>
#include <cstring>
int main() {
    uint32_t words[32],expected[32];
    for(unsigned i=0;i<32;++i) {
#if WRITE_CASE==5
        words[i]=0x3d000000u|((i*0x9e3779b9u)&0x01ffffffu);
#else
        words[i]=0x12560000u+i*13+7;
#endif
        expected[i]=words[i];
    }
    cpphdl::hls::bind_external_memory(words,sizeof(words),0x1000);
    for(unsigned id=0;id<128;++id) {
        unsigned index=id%4;uint32_t value=0xfedc0000u+id*97u;
        uint64_t result;
#if WRITE_CASE==0 || WRITE_CASE==6
        expected[index]=value;result=value;
#elif WRITE_CASE==1
        expected[index]+=value;result=expected[index];
#elif WRITE_CASE==2
        if(value&1)expected[index]=value^0xabc;
        result=expected[index];
#elif WRITE_CASE==3
        expected[index]=value;expected[index+1]=value+1;
        result=uint64_t(value)+uint32_t(value+1);
#elif WRITE_CASE==4
        expected[index]=(expected[index]&0xffffu)|(value<<16);result=expected[index];
#elif WRITE_CASE==5
        value=0x3d000000u|((id*0x9e3779b9u)&0x01ffffffu);
        float a=cpphdl::hls::math::decode(expected[index]),b=cpphdl::hls::math::decode(expected[16+index]);
        volatile float product=b*cpphdl::hls::math::decode(value);
        expected[index]=cpphdl::hls::math::encode(a+product);result=expected[index];
#else
        if(value&1)expected[index]=value;
        result=value+3;
#endif
        auto actual=SourceWriteMethods{}.command(0x1000,index,value);
        if(actual!=result || std::memcmp(words,expected,sizeof(words)))return 1;
    }
    std::puts("PASS native source writes and cross-call aliases");
}
#endif
