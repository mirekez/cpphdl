#include "../Clocked.h"
#include <cstdint>

#ifndef READ_WINDOW
#define READ_WINDOW 2
#endif
#ifndef DEPENDENT_SECOND_READ
#define DEPENDENT_SECOND_READ 0
#endif

struct ConcurrentReadMethods {
    uint64_t command(uint32_t first_address, uint32_t second_base,
                     uint32_t output_address) const {
        uint64_t first = *cpphdl::hls::external_memory<uint64_t>(first_address);
#if DEPENDENT_SECOND_READ
        uint64_t second = *cpphdl::hls::external_memory<uint64_t>(
            second_base + uint32_t(first & 3u) * 8u);
#else
        uint64_t second = *cpphdl::hls::external_memory<uint64_t>(second_base);
#endif
        uint64_t total = first + second;
        if (output_address)
            *cpphdl::hls::external_memory<uint64_t>(output_address) = total;
        return total;
    }
};

cpphdl::hls::ClockedMemory<ConcurrentReadMethods,32,false,64,READ_WINDOW> worker;

#ifndef SYNTHESIS
#include <array>
#include <cstdio>
int main() {
    std::array<uint64_t,32> words{};
    words[0]=3;
    words[2]=101; words[3]=203; words[4]=307; words[5]=409;
    cpphdl::hls::bind_external_memory(words.data(),words.size()*8,0x1000);
    ConcurrentReadMethods method;
    const uint64_t expected=3+(DEPENDENT_SECOND_READ ? 409u : 101u);
    if(method.command(0x1000,0x1010,0x1080)!=expected || words[16]!=expected)
        return 1;
    std::puts("PASS native concurrent-read method");
}
#endif
