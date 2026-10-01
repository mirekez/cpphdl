#include "model.h"
#include <cstdio>
int main() {
    cpphdl_native::Model model;
    for (unsigned sample = 0; sample < 16; ++sample) {
        model.enable_i[0] = sample & 1;
        model.select_i[0] = sample >> 1;
        unsigned tree = sample & 1;
        for (unsigned node = 0; node < 3; ++node) {
            const auto active = (tree >> node) & 1;
            const auto select = (model.select_i[0] >> node) & 1;
            tree |= (active & (select ^ 1)) << (2 * node + 1);
            tree |= (active & select) << (2 * node + 2);
        }
        model.eval();
        if (model.tree_o[0] != tree) return 1;
    }
    std::puts("ordinary C++ graph: concurrent forward-reference tree matches all inputs");
}
