#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace fp_reference {
inline float value(uint32_t bits) {
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}
inline uint32_t bits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
inline uint32_t flush(uint32_t raw) {
    if (!(raw & 0x7f800000u))
        return raw & 0x80000000u;
    if ((raw & 0x7fffffffu) > 0x7f800000u)
        return 0x7fc00000u;
    return raw;
}
inline uint32_t add(uint32_t a, uint32_t b) {
    return flush(bits(value(flush(a)) + value(flush(b))));
}
inline uint32_t multiply(uint32_t a, uint32_t b) {
    return flush(bits(value(flush(a)) * value(flush(b))));
}
inline uint32_t half(uint16_t raw) {
    unsigned exponent = (raw >> 10) & 31, mantissa = raw & 1023;
    if (exponent == 31)
        return (uint32_t(raw & 0x8000u) << 16) | 0x7f800000u | (mantissa << 13);
    float result = exponent ? std::ldexp(float(1024 + mantissa), int(exponent) - 25)
                            : std::ldexp(float(mantissa), -24);
    return bits(raw & 0x8000u ? -result : result);
}
inline uint32_t random(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
inline void check(const char *operation, uint64_t actual, uint64_t expected) {
    if (actual != expected) {
        std::fprintf(stderr, "%s: got %016llx, expected %016llx\n", operation,
                     (unsigned long long)actual, (unsigned long long)expected);
        throw std::runtime_error("floating-point RTL mismatch");
    }
}
inline constexpr uint32_t edges[] = {0,           0x80000000u, 1,           0x007fffffu,
                                     0x00800000u, 0x00800001u, 0x3f000000u, 0x3f800000u,
                                     0x3f800001u, 0x3fffffffu, 0xbf800000u, 0x7f7fffffu,
                                     0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7f800001u};
} // namespace fp_reference
