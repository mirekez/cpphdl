#include "../Clocked.h"
#ifndef STREAM_BITS
#define STREAM_BITS 512
#endif
struct ReadGroupPacket {uint32_t lane[STREAM_BITS/32];};
struct WideReadGroupMethods {
    uint64_t command(uint32_t source,uint32_t count,uint32_t selection) const {
        ReadGroupPacket local[2];
        auto input=cpphdl::hls::external_memory<const ReadGroupPacket>(source);
        local[0]=cpphdl::hls::load_external(input);
        local[1]=cpphdl::hls::load_external(input+1);
        ReadGroupPacket result{};
        for(unsigned round=0;round<count;++round) {
            auto left=local[selection&1];
            auto right=local[(selection>>1)&1];
#pragma omp simd
            for(unsigned lane=0;lane<STREAM_BITS/32;++lane)
                result.lane[lane]+=left.lane[lane]^(right.lane[lane]+round);
        }
        uint32_t sum=0;
#pragma omp simd reduction(+:sum)
        for(unsigned lane=0;lane<STREAM_BITS/32;++lane)sum+=result.lane[lane];
        return sum;
    }
};
cpphdl::hls::ClockedMemory<WideReadGroupMethods,32,true,STREAM_BITS,8,true> worker;
#ifndef SYNTHESIS
#include <array>
#include <cstdio>
int main() {
    constexpr unsigned words=STREAM_BITS/32;
    alignas(64) std::array<uint32_t,2*words> memory{};
    for(unsigned i=0;i<memory.size();++i)memory[i]=0xf0000100u+i*7919;
    cpphdl::hls::bind_external_memory(memory.data(),sizeof(memory),0x1000);
    for(unsigned count=0;count<=5;++count)for(unsigned selection=0;selection<4;++selection) {
        uint32_t want=0;
        for(unsigned round=0;round<count;++round)for(unsigned lane=0;lane<words;++lane)
            want+=memory[(selection&1)*words+lane]^(memory[((selection>>1)&1)*words+lane]+round);
        if(WideReadGroupMethods{}.command(0x1000,count,selection)!=want)return 1;
    }
    std::puts("PASS native wide local read groups");
}
#endif
