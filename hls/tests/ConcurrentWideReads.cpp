#include "../Clocked.h"
#include <cstdint>

struct ReadBeat {uint32_t lane[16];};
static_assert(sizeof(ReadBeat)==64);
struct ConcurrentWideMethods {
    uint64_t command(uint32_t a,uint32_t b,uint32_t) const {
        ReadBeat first=cpphdl::hls::load_external(
            cpphdl::hls::external_memory<const ReadBeat>(a));
        ReadBeat second=cpphdl::hls::load_external(
            cpphdl::hls::external_memory<const ReadBeat>(b));
        return uint64_t(first.lane[15])+second.lane[14];
    }
};
cpphdl::hls::ClockedMemory<ConcurrentWideMethods,32,false,512,2> worker;

#ifndef SYNTHESIS
#include <array>
#include <cstdio>
int main() {
    std::array<uint32_t,32> words{};
    for(unsigned i=0;i<32;++i)words[i]=0x12000000u+i*0x01020304u;
    cpphdl::hls::bind_external_memory(words.data(),sizeof(words),0x1000);
    if(ConcurrentWideMethods{}.command(0x1000,0x1040,0)!=
       uint64_t(words[15])+words[30])return 1;
    std::puts("PASS native concurrent wide reads");
}
#endif
