#pragma once

#include "cpphdl.h"
#include <array>
#include <cstring>

namespace cpphdl::graph_runtime {

// Host logic stores bytes least significant first; native ports store numeric
// 32-bit words. Transfer complete bytes/words at the boundary instead of
// reconstructing every bit. Mask padding even if the source contains junk.
template<size_t Width, size_t Words>
inline void pack(std::array<uint32_t, Words>& port, const logic<Width>& value) {
    static_assert(Words >= (Width + 31) / 32);
    port.fill(0);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    std::memcpy(port.data(), value.bytes, value.SIZE);
#else
    for (size_t byte = 0; byte < value.SIZE; ++byte)
        port[byte / 4] |= uint32_t(value.bytes[byte]) << (8 * (byte % 4));
#endif
    if constexpr (Width % 32) port[Width / 32] &= (uint32_t(1) << (Width % 32)) - 1;
}

template<size_t Width, size_t Words>
inline void unpack(logic<Width>& value, const std::array<uint32_t, Words>& port) {
    static_assert(Words >= (Width + 31) / 32);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    std::memcpy(value.bytes, port.data(), value.SIZE);
#else
    for (size_t byte = 0; byte < value.SIZE; ++byte)
        value.bytes[byte] = uint8_t(port[byte / 4] >> (8 * (byte % 4)));
#endif
    if constexpr (Width % 8) value.bytes[value.SIZE - 1] &= (1u << (Width % 8)) - 1;
}
}
