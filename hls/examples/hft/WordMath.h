#pragma once
#include <cstdint>

// Fixed-width parallel carry networks for one-word-per-clock recurrences.
struct HftWordMath {
    static uint32_t add(uint32_t a, uint32_t b) {
        uint32_t propagate, carries, shift;
        propagate = a ^ b; carries = a & b;
        for (shift = 1; shift < 32; shift <<= 1) {
            carries |= propagate & (carries << shift);
            propagate &= propagate << shift;
        }
        return a ^ b ^ (carries << 1);
    }
    static bool less(uint32_t a, uint32_t b) {
        uint32_t propagate, carries, shift;
        propagate = a ^ ~b; carries = a & ~b;
        for (shift = 1; shift < 32; shift <<= 1) {
            carries |= propagate & (carries << shift);
            propagate &= propagate << shift;
        }
        // Carry-out of a + ~b + 1 means a >= b.
        return ((carries | propagate) >> 31) == 0;
    }
};
