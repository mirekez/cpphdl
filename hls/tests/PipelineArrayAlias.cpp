#include "../Clocked.h"
#include <cstdint>

struct AliasPacket {
    uint32_t word[4];
    uint32_t other[2];
    uint32_t tag;
};
struct AliasMethods {
    static uint32_t by_value(AliasPacket copy, uint32_t index) {
        auto word = copy.word; // C array decay names this copy's subobject.
        uint32_t sum = word[0] + word[index & 3] + word[3];
        word[0] = sum + 7;
        return word[0] ^ copy.tag;
    }
    uint32_t command(AliasPacket packet, AliasPacket, AliasPacket) const {
#if defined(ALIAS_NEGATIVE_REBIND)
        auto word = packet.word;
        word = packet.other;
        return word[0];
#elif defined(ALIAS_NEGATIVE_ESCAPE)
        auto word = packet.word;
        return reinterpret_cast<uintptr_t>(word) & 0xffffffffu;
#elif defined(ALIAS_NEGATIVE_VOLATILE)
        volatile uint32_t* word = packet.word;
        return word[0];
#elif defined(ALIAS_NEGATIVE_REINTERPRET)
        auto word = packet.word;
        auto wide = reinterpret_cast<uint64_t*>(word);
        return uint32_t(wide[0]);
#else
        uint32_t original = packet.word[0];
        return by_value(packet, packet.tag) + original;
#endif
    }
};
cpphdl::hls::ClockedPipeline<AliasMethods, 1, AliasPacket, uint32_t> alias_worker;

#ifndef SYNTHESIS
#include <cstdio>
int main() {
    AliasPacket packet{{11, 13, 17, 19}, {23, 29}, 2};
    AliasMethods methods;
    uint32_t expected = ((11u + 17u + 19u + 7u) ^ 2u) + 11u;
    if (methods.command(packet, {}, {}) != expected || packet.word[0] != 11 ||
        AliasMethods::by_value(packet, 0) != ((11u + 11u + 19u + 7u) ^ 2u) ||
        packet.word[0] != 11) return 1;
    std::puts("PASS native by-value packet array alias and isolation");
}
#endif
