#include "model.h"
#include <cstdint>
#include <iostream>

int main() {
    cpphdl_native::Model model;
    uint64_t seed = 0x123456789abcdefull;
    for (unsigned sample = 0; sample < 65536; ++sample) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        const auto data = seed;
        const auto other = sample < 256 ? uint64_t(sample) : ~seed * 17;
        const bool enable = sample & 1;
        model.data_in = {uint32_t(data), uint32_t(data >> 32)};
        model.other_in = {uint32_t(other), uint32_t(other >> 32)};
        model.enable_in[0] = enable;
        model.eval();
        const auto expected = ((enable ? data : other) & 0x00ff00ff00ff00ffull) ^ (data >> 7);
        const auto result = uint64_t(model.result_out[0]) | (uint64_t(model.result_out[1]) << 32);
        const auto sum = uint64_t(model.sum_out[0]) | (uint64_t(model.sum_out[1]) << 32);
        if (result != expected || sum != data + other || !model.wide_out[0])
            return 1;
    }
    std::cout << "graph construction: 65536 mixed-operation samples passed\n";
}
