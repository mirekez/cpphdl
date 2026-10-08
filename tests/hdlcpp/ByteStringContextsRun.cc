#include <cstdio>
#include <cstdint>
#ifdef STRING_GRAPH
#include "model.h"
#endif
#ifdef STRING_RTL
#include "VByteStringContexts.h"
#else
#include "generated/ByteStringContexts.h"
long _system_clock = 0;
#endif
int main() {
    const uint32_t expected[] = {
        0, 68, 0x4142, 10, 9, 34, 92, 255,
        65, 0x410042, 77, 0, 0x414243, 0x42, 0x43564136, 10
    };
#ifdef STRING_RTL
    VByteStringContexts dut;
#else
    ByteStringContexts<> dut;
    cpphdl::logic<5> selected;
    dut.select_i_in = _ASSIGN(selected);
    dut._assign();
#endif
#ifdef STRING_GRAPH
    cpphdl_native::Model graph;
#endif
    for (unsigned index = 0; index < 32; ++index) {
        uint32_t wide[4] = {};
        const uint32_t expected_wide[] = {
            index & 16 ? 0x494a4b4cU : 0,
            index & 16 ? 0x45464748U : 0,
            index & 16 ? 0x41424344U : 0, 0
        };
        unsigned wanted = (index == 27) ? 0x5859 : expected[index & 15];
#ifdef STRING_RTL
        dut.select_i = index;
        dut.eval();
        unsigned actual = dut.value_o;
        bool string_ok = dut.string_ok_o;
        unsigned math = dut.math_o;
        bool equal = dut.equal_o;
        for (unsigned word = 0; word < 4; ++word) wide[word] = dut.wide_o[word];
#else
        selected = index;
        ++_system_clock;
        unsigned actual = uint64_t(dut.value_o_out());
        bool string_ok = bool(dut.string_ok_o_out());
        unsigned math = uint64_t(dut.math_o_out());
        bool equal = bool(dut.equal_o_out());
        auto packed = dut.wide_o_out();
        for (unsigned word = 0; word < 4; ++word)
            wide[word] = uint64_t(packed.bits(word * 32 + 31, word * 32));
#endif
#ifdef STRING_GRAPH
        graph.select_i[0] = index;
        graph.eval(false);
        if (graph.value_o[0] != actual || bool(graph.string_ok_o[0]) != string_ok) return 2;
        if (graph.math_o[0] != math || bool(graph.equal_o[0]) != equal) return 2;
        for (unsigned word = 0; word < 4; ++word)
            if (graph.wide_o[word] != wide[word]) return 2;
#endif
        if (actual != wanted || !string_ok) {
            std::fprintf(stderr, "select=%u actual=%x expected=%x string_ok=%u\n",
                         index, actual, wanted, string_ok);
            return 1;
        }
        if (math != 65 + index || equal != (index == 4)) return 3;
        for (unsigned word = 0; word < 4; ++word) {
            if (wide[word] != expected_wide[word]) {
                std::fprintf(stderr, "select=%u word=%u actual=%x expected=%x\n",
                             index, word, wide[word], expected_wide[word]);
                return 1;
            }
        }
    }
    std::puts("string integral contexts: 32 oracle comparisons passed");
}
