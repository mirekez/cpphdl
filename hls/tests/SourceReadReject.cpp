#include "../Clocked.h"
#ifndef READ_REJECT_CASE
#define READ_REJECT_CASE 0
#endif
struct Methods {
#if READ_REJECT_CASE==4
    uint64_t command(uint64_t address,uint64_t index,uint64_t value) const {
#else
    uint64_t command(uint32_t address,uint32_t index,uint32_t value) const {
#endif
#if READ_REJECT_CASE==5
        auto memory=cpphdl::hls::external_memory<volatile uint32_t>(address);
#else
        auto memory=cpphdl::hls::external_memory<uint32_t>(address);
#endif
#if READ_REJECT_CASE==0
        return memory[index]+memory[index+1];
#elif READ_REJECT_CASE==1
        return value?memory[index]:0;
#elif READ_REJECT_CASE==2
        memory[index]=value;return value;
#elif READ_REJECT_CASE==3
        uint32_t sum=0;
        for(uint32_t i=0;i<index;++i)sum+=memory[i];
        return sum;
#else
        return memory[index]+value;
#endif
    }
};
cpphdl::hls::ClockedMemory<Methods,32,false,64,8,true> worker;
