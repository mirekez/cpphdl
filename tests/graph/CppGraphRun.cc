#include "model.h"
#include <cstdint>
#include <cstdio>
int main() {
    cpphdl_native::Model model;
    uint32_t random = 19;
    uint8_t state = 0;
    for (unsigned sample = 0; sample < 20000; ++sample) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        model.rst_ni[0] = sample % 83 != 0;
        model.choose_i[0] = random & 1;
        model.data_i[0] = (random >> 8) & 255;
        model.other_i[0] = (random >> 16) & 255;
        model.eval();
        if (model.state_o[0] != state || model.result_o[0] != (state ^ model.data_i[0])) return 1;
        state = !model.rst_ni[0] ? 0 : model.choose_i[0] ? model.data_i[0] + model.other_i[0] : model.data_i[0] ^ model.other_i[0];
        model.step();
        if (model.state_o[0] != state || model.result_o[0] != (state ^ model.data_i[0])) return 2;
    }
    std::puts("ordinary C++ graph: 20000 state/branch samples passed");
}
