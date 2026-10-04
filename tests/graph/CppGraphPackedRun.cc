#include "model.h"
#include <cstdint>
#include <cstdio>

int main() {
    cpphdl_native::Model model;
    uint64_t random = 0x1234567812345678ull;
    uint32_t expected[4]{};
    for (unsigned sample = 0; sample < 2000; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        model.first_i[0] = random; model.first_i[1] = random >> 32;
        model.second_i[0] = ~random; model.second_i[1] = ~(random >> 32);
        model.rst_ni[0] = sample % 79 != 0;
        model.enable_i[0] = random & 1;
        if (!model.rst_ni[0]) {
            for (auto& word : expected) word = 0;
        } else {
            expected[0] = model.first_i[0]; expected[1] = model.first_i[1];
            if (model.enable_i[0]) {
                expected[2] = model.second_i[0]; expected[3] = model.second_i[1];
            }
        }
        model.step();
        for (unsigned word = 0; word < 4; ++word) {
            auto combined = word < 2 ? model.first_i[word] : model.second_i[word - 2];
            if (model.result_o[word] != combined || model.state_o[word] != expected[word]) return 1;
        }
    }
    std::puts("ordinary SV-to-C++ graph: 2000 nested packed-array comb/partial-NBA samples passed");
}
