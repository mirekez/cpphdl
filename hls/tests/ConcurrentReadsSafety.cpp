#include "../Clocked.h"
#include <cstdint>

#ifndef READ_PATTERN
#error READ_PATTERN must select a read grouping safety case
#endif

struct ReadSafetyMethods {
    uint64_t command(uint32_t first_address,uint32_t second_base,uint32_t) const {
#if READ_PATTERN == 1
        volatile uint64_t first = *cpphdl::hls::external_memory<volatile uint64_t>(first_address);
        uint64_t second = *cpphdl::hls::external_memory<uint64_t>(second_base);
        return first+second;
#elif READ_PATTERN == 2
        uint64_t first=*cpphdl::hls::external_memory<uint64_t>(first_address);
        uint64_t carry=first+second_base;
        second_base+=8;
        uint64_t second=*cpphdl::hls::external_memory<uint64_t>(second_base);
        return carry+second;
#elif READ_PATTERN == 3
        uint64_t first=*cpphdl::hls::external_memory<uint64_t>(first_address);
        uint64_t second=0;
        if(first&1u)second=*cpphdl::hls::external_memory<uint64_t>(second_base);
        return first+second;
#elif READ_PATTERN == 4
        uint64_t first=*cpphdl::hls::external_memory<uint64_t>(first_address);
        uint64_t second=*cpphdl::hls::external_memory<uint64_t>(second_base);
        uint64_t third=*cpphdl::hls::external_memory<uint64_t>(second_base+8);
        uint64_t fourth=*cpphdl::hls::external_memory<uint64_t>(second_base+16);
        return first+second+third+fourth;
#else
#error unsupported READ_PATTERN
#endif
    }
};

cpphdl::hls::ClockedMemory<ReadSafetyMethods,32,false,64,2> worker;

#ifndef SYNTHESIS
#include <array>
#include <cstdio>
int main() {
    std::array<uint64_t,32> words{};
    words[0]=3;words[1]=2;words[2]=101;words[3]=203;
    words[4]=307;words[5]=409;
    cpphdl::hls::bind_external_memory(words.data(),words.size()*8,0x1000);
    ReadSafetyMethods method;
    const uint64_t expected=READ_PATTERN==2 ? 4318u : READ_PATTERN==4 ? 614u : 104u;
    if(method.command(0x1000,0x1010,0)!=expected)return 1;
    if(READ_PATTERN==3 && method.command(0x1008,0x1010,0)!=2)return 2;
    std::puts("PASS native concurrent-read safety");
}
#endif
