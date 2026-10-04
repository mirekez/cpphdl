#include "ZeroConcatCases.cc"
#include "model.h"
#include <cstdio>
long _system_clock = 0;

struct ZeroConcatCasesDriver : Module {
    logic<32> data;
    ZeroConcatCases dut;
    void _assign() {
        dut.data_in = _ASSIGN(data);
        dut._assign();
    }
};

int main() {
    cpphdl_native::Model model;
    ZeroConcatCasesDriver driver;
    auto& cpphdl_top = driver.dut;
    auto& data = driver.data;
    uint32_t random = 0x12345678;
    driver._assign();
    for (unsigned sample = 0; sample < 2048; ++sample) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        uint32_t input = sample == 0 ? 0 : sample == 1 ? ~0u : random;
        data = input;
        model.data[0] = input;
        ++_system_clock;
        model.eval();
        if (uint32_t(cpphdl_top.leading_out()) != input || model.leading[0] != input ||
            uint32_t(cpphdl_top.trailing_out()) != input || model.trailing[0] != input ||
            uint32_t(cpphdl_top.nested_out()) != input || model.nested[0] != input ||
            uint64_t(cpphdl_top.middle_out()) != (0xa500000000ull | input) ||
            model.middle[0] != input || model.middle[1] != 0xa5 ||
            uint32_t(cpphdl_top.effects_out()) != 0x030103 || model.effects[0] != 0x030103 ||
            uint32_t(cpphdl_top.references_out()) != 0x0202 || model.references[0] != 0x0202)
            return 1;
        auto wide = cpphdl_top.wide_out();
        for (unsigned bit = 0; bit < 73; ++bit) {
            bool expected = bit < 4 ? ((9u >> bit) & 1) : bit < 68 ?
                ((input >> ((bit - 4) % 32)) & 1) : ((0x15u >> (bit - 68)) & 1);
            if (wide.get(bit) != expected || ((model.wide[bit / 32] >> (bit % 32)) & 1) != expected)
                return 2;
        }
    }
    std::puts("empty concatenation: 2048 C++/graph samples, wide packing and ordered side effects passed");
}
