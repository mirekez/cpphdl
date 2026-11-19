#include "../Clocked.h"
#include <cstdint>

#if WIDE_REJECT == 1
struct Packet { uint8_t tag; uint32_t payload; }; // padding in an 8-byte object
#elif WIDE_REJECT == 2
struct Packet { uint32_t lane[32]; }; // 128 bytes, above the 512-bit maximum
#elif WIDE_REJECT == 3
struct Packet { uint32_t lane[16]; }; // 64 bytes, above the default 64-bit port
#else
#error WIDE_REJECT must be 1, 2, or 3
#endif

struct WideRejectedMethods {
    uint64_t command(uint32_t source, uint32_t, uint32_t) const {
        auto value = cpphdl::hls::load_external(
            cpphdl::hls::external_memory<const Packet>(source));
#if WIDE_REJECT == 1
        return value.payload;
#else
        return value.lane[0];
#endif
    }
};

#if WIDE_REJECT == 3
cpphdl::hls::ClockedMemory<WideRejectedMethods> worker;
#else
cpphdl::hls::ClockedMemory<WideRejectedMethods,32,false,512> worker;
#endif
