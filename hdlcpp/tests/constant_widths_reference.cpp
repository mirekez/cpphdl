#include "Vconstant_widths_reference.h"
#include <cstdint>
#include <cstdio>

int main()
{
    Vconstant_widths_reference model;
    model.eval();
    constexpr uint64_t expected[3][6] = {
        {8, 0x80000000ull, 127, 127, 5, 0x100000000ull},
        {16, 0x7fffffffffffffffull, 64, 127, 9, 0x100000000ull},
        {4, 0x4000000000000000ull, 127, 127, 2, 0x100000000ull},
    };
    for (unsigned instance = 0; instance < 3; ++instance) {
        for (unsigned port = 0; port < 6; ++port) {
            if (model.values[instance][port] != expected[instance][port]) {
                std::fprintf(stderr, "SV reference mismatch: instance %u port %u\n", instance, port);
                return 1;
            }
        }
    }
    std::puts("constant widths: SystemVerilog reference matches the boundary oracle");
}
