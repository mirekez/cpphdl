#include <unordered_map>
#include <cstring>
#include <cstdio>
#include "UnorderedMapOverrides.h"

uint32_t bits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
int main() {
    using namespace unordered_map_rtl;
    for (size_t value = 0; value <= 4096; ++value) {
        if (next_prime(value) != std::__next_prime(value)) {
            std::fprintf(stderr, "next_prime(%zu): expected %zu got %zu\n", value, std::__next_prime(value), next_prime(value));
            return 1;
        }
        uint32_t encoded = bits(float(value));
        if (from_size(value) != encoded || to_size(encoded) != value || ceil_bits(encoded) != encoded ||
            multiply(encoded, bits(1.0f)) != encoded || divide(encoded, bits(1.0f)) != encoded ||
            greater(encoded, bits(127.0f)) != (value > 127)) return 2;
        if (population(value) != __builtin_popcountll(value) ||
            leading_zeros(value, 64) != (value ? __builtin_clzll(value) : 64)) return 3;
    }
    for (uint64_t value : {uint64_t(0x7fffff), uint64_t(0x800000), uint64_t(0xffffff), uint64_t(0x1000000)})
        if (from_size(value) != bits(float(value)) || to_size(bits(float(value))) != value) return 4;
    std::puts("override helpers match native prime and floating-point behavior in their declared domain");
}
