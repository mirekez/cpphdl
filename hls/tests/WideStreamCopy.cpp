#include "../Clocked.h"
#ifndef STREAM_BITS
#define STREAM_BITS 512
#endif
struct StreamPacket {uint32_t lane[STREAM_BITS/32];};
struct WideStreamCopyMethods {
    uint64_t command(uint32_t source,uint32_t count,uint32_t destination) const {
        StreamPacket local[4];
        auto input=cpphdl::hls::external_memory<const StreamPacket>(source);
        auto output=cpphdl::hls::external_memory<StreamPacket>(destination);
        for(unsigned i=0;i<count;++i)local[i&3]=cpphdl::hls::load_external(input+i);
        for(unsigned i=0;i<(count<4?count:4);++i) {
            auto packet=local[i];
            packet.lane[0]^=count;packet.lane[STREAM_BITS/32-1]+=i;
            cpphdl::hls::store_external(output+i,packet);
        }
        return count?local[(count-1)&3].lane[STREAM_BITS/32-1]:0;
    }
};
cpphdl::hls::ClockedMemory<WideStreamCopyMethods,32,true,STREAM_BITS,8,true> worker;
#ifndef SYNTHESIS
#include <array>
#include <cstdio>
int main() {
    constexpr unsigned words=STREAM_BITS/32;
    alignas(64) std::array<uint32_t,1024> memory{};
    for(unsigned i=0;i<16*words;++i)memory[i]=0x10000+i*17;
    cpphdl::hls::bind_external_memory(memory.data(),sizeof(memory),0x1000);
    for(unsigned count:{0u,1u,2u,4u,8u,16u}) {
        const auto got=WideStreamCopyMethods{}.command(0x1000,count,0x1800);
        if(got!=(count?memory[(count-1)*words+words-1]:0))return 1;
        for(unsigned slot=0;slot<(count<4?count:4);++slot) {
            const auto source=slot+4*((count-1-slot)/4);
            for(unsigned lane=0;lane<words;++lane) {
                auto want=memory[source*words+lane];
                if(lane==0)want^=count;if(lane==words-1)want+=slot;
                if(memory[512+slot*words+lane]!=want)return 2;
            }
        }
    }
    std::puts("PASS native wide stream copy and ordered local aliases");
}
#endif
