#pragma once
#include "../../Overrides.h"
#include <cstdint>
#include <cstddef>

// Test policy: load factor stays 1 and all sizes fit in 24 bits. These helpers
// preserve IEEE binary32 bits, but reject fractions, negatives, and overflow.
namespace unordered_map_rtl {
HLS_OVERRIDE("__builtin_clzg")
inline int leading_zeros(uint64_t value, int zero_result) {
    if (!value) return zero_result;
    int count = 0;
    uint64_t mask = uint64_t(1) << 63;
    while ((value & mask) == 0) { ++count; mask >>= 1; }
    return count;
}
HLS_OVERRIDE("__builtin_popcountg")
inline int population(uint64_t value) {
    int count = 0;
    while (value) { count += int(value & 1u); value >>= 1; }
    return count;
}
inline uint32_t integer(uint32_t bits) {
    if (bits == 0) return 0;
    uint32_t exponent = (bits >> 23) & 255u;
    if ((bits >> 31) || exponent < 127 || exponent > 151) __builtin_trap();
    uint32_t significand = (bits & 0x7fffffu) | 0x800000u;
    if (exponent == 151) {
        if (significand != 0x800000u) __builtin_trap();
        return 0x1000000u;
    }
    uint32_t shift = 150 - exponent;
    if ((significand & ((1u << shift) - 1u)) != 0) __builtin_trap();
    return significand >> shift;
}
HLS_OVERRIDE_BITS("@float.cast.u64.f32")
inline uint32_t from_size(uint64_t value) {
    if (value > 0x1000000u) __builtin_trap();
    if (value == 0) return 0;
    uint32_t exponent = 0, scan = uint32_t(value);
    while (scan > 1) { scan >>= 1; ++exponent; }
    uint32_t fraction = exponent == 24 ? 0 : (uint32_t(value) << (23 - exponent)) & 0x7fffffu;
    return ((exponent + 127) << 23) | fraction;
}
HLS_OVERRIDE_BITS("@float.cast.f32.u64")
inline uint64_t to_size(uint32_t value) { return integer(value); }
HLS_OVERRIDE_BITS("@float.*.f32.f32.f32")
inline uint32_t multiply(uint32_t lhs, uint32_t rhs) {
    return from_size(uint64_t(integer(lhs)) * integer(rhs));
}
HLS_OVERRIDE_BITS("@float./.f32.f32.f32")
inline uint32_t divide(uint32_t lhs, uint32_t rhs) {
    if (rhs != 0x3f800000u) __builtin_trap();
    integer(lhs);
    return lhs;
}
HLS_OVERRIDE_BITS("@float.>.f32.f32.b1")
inline bool greater(uint32_t lhs, uint32_t rhs) { return integer(lhs) > integer(rhs); }
HLS_OVERRIDE_BITS("__builtin_ceilf")
inline uint32_t ceil_bits(uint32_t value) { integer(value); return value; }

HLS_OVERRIDE("std::__next_prime")
inline size_t next_prime(size_t value) {
    // A bounded trial-division implementation, not a table of test answers.
    if (value > 4096) __builtin_trap();
    if (value == 0) return 0;
    if (value <= 2) return 2;
    uint16_t candidate = uint16_t(value);
    if ((candidate & 1u) == 0) ++candidate;
    while (true) {
        bool prime = true;
        for (uint16_t divisor = 3; uint32_t(divisor) * divisor <= candidate; divisor += 2) {
            if (candidate % divisor == 0) { prime = false; break; }
        }
        if (prime) return candidate;
        candidate += 2;
    }
}
}
