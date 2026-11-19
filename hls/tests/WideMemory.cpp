#include "../Clocked.h"
#include <cstdint>

struct WideBeat { uint32_t lane[16]; };
static_assert(sizeof(WideBeat) == 64);

struct WideMemoryMethods {
    uint64_t command(uint32_t source, uint32_t scalar_address, uint32_t destination) const {
        WideBeat packet = cpphdl::hls::load_external(
            cpphdl::hls::external_memory<const WideBeat>(source));
        uint32_t scalar = *cpphdl::hls::external_memory<uint32_t>(scalar_address);
        // The aggregate pointer may start 4 bytes past a packet boundary when
        // only a naturally aligned scalar field is accessed.
        uint32_t member = cpphdl::hls::external_memory<const WideBeat>(source+4)->lane[2];
        packet.lane[0] ^= scalar;
        packet.lane[7] += member;
        packet.lane[15] ^= scalar + member;
        cpphdl::hls::store_external(cpphdl::hls::external_memory<WideBeat>(destination), packet);
        return (uint64_t(packet.lane[15]) << 32) | packet.lane[7];
    }
};

cpphdl::hls::ClockedMemory<WideMemoryMethods, 32, false, 512> worker;

#ifndef SYNTHESIS
#include <array>
#include <cstdio>
int main() {
    std::array<uint32_t, 64> words{};
    for (unsigned lane=0;lane<16;++lane) words[lane]=0x11000000u+lane*0x01010101u;
    words[16]=0x13579bdfu;
    cpphdl::hls::bind_external_memory(words.data(), words.size()*4, 0x1000);
    WideMemoryMethods method;
    const auto result=method.command(0x1000,0x1040,0x1080);
    const uint32_t high=(0x11000000u+15*0x01010101u) ^ (words[16]+words[3]);
    const uint32_t low=(0x11000000u+7*0x01010101u)+words[3];
    for(unsigned lane=0;lane<16;++lane) {
        uint32_t expected=0x11000000u+lane*0x01010101u;
        if(lane==0) expected^=words[16];
        if(lane==7) expected+=words[3];
        if(lane==15) expected^=words[16]+words[3];
        if(words[32+lane]!=expected)return 1;
    }
    if(result!=((uint64_t(high)<<32)|low))return 2;
    std::puts("PASS native wide memory aggregate/scalar/member");
}
#endif
