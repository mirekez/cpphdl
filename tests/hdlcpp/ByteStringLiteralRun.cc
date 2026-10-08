#include <cstdio>
#include <cstdint>
#ifdef STRING_GRAPH
#include "model.h"
#endif
#ifdef STRING_RTL
#include "VByteStringLiteral.h"
#else
#include "generated/ByteStringLiteral.h"
long _system_clock = 0;
#endif
int main() {
    unsigned errors = 0;
    const unsigned expected[] = {68, 77, 83, 85};
#ifdef STRING_RTL
    VByteStringLiteral dut;
#else
    ByteStringLiteral dut;
    cpphdl::logic<2> selected;
    dut.select_i_in = _ASSIGN(selected);
    dut._assign();
#endif
#ifdef STRING_GRAPH
    cpphdl_native::Model graph;
#endif
    for (unsigned index = 0; index < 4; ++index) {
#ifdef STRING_RTL
        dut.select_i = index;
        dut.eval();
        unsigned actual = dut.mode_o;
#else
        selected = index;
        ++_system_clock;
        unsigned actual = uint64_t(dut.mode_o_out()) & 255;
#endif
#ifdef STRING_GRAPH
        graph.select_i[0] = index;
        graph.eval(false);
        if (graph.mode_o[0] != actual) {
            return 2;
        }
#endif
        std::printf("select=%u actual=%u expected=%u\n", index, actual, expected[index]);
        errors += actual != expected[index];
    }
    return errors ? 1 : 0;
}
