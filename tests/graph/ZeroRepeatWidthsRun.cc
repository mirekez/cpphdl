#include "generated/ZeroRepeatWidths.h"
#include "model.h"
#include "VZeroRepeatWidths.h"
#include <cstdio>

long _system_clock = 0;
struct ZeroRepeatWidthsDriver : cpphdl::Module {
    cpphdl::logic<ZERO_VLEN> address;
    ZeroRepeatWidths<ZERO_XLEN, ZERO_VLEN> dut;
    void _assign() {
        dut.addr_i_in = _ASSIGN(address);
        dut._assign();
    }
};

int main() {
    cpphdl_native::Model graph;
    ZeroRepeatWidthsDriver driver;
    auto& cpp = driver.dut;
    auto& address = driver.address;
    VZeroRepeatWidths rtl;
    constexpr unsigned padding = ZERO_XLEN - ZERO_VLEN;
    constexpr uint64_t inputMask = (1ull << ZERO_VLEN) - 1;
    constexpr uint64_t outputMask = (1ull << ZERO_XLEN) - 1;
    uint32_t random = 0x12345678;
    driver._assign();
    for (unsigned sample = 0; sample < 2048; ++sample) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        uint64_t input = (sample == 0 ? 0 : sample == 1 ? inputMask : random) & inputMask;
        uint64_t extended = input | ((input >> (ZERO_VLEN - 1)) ? (outputMask ^ inputMask) : 0);
        uint64_t tail = (input << padding) | ((1ull << padding) - 1);
        uint64_t middle = (0xa5ull << ZERO_XLEN) | extended;
        address = input;
        graph.addr_i[0] = input;
        rtl.addr_i = input;
        ++_system_clock;
        graph.eval();
        rtl.eval();
        auto read = [](const auto& words) {
            uint64_t value = words[0];
            if (words.size() > 1) value |= uint64_t(words[1]) << 32;
            return value;
        };
        if (uint64_t(cpp.result_o_out()) != extended || read(graph.result_o) != extended || rtl.result_o != extended ||
            uint64_t(cpp.tail_o_out()) != tail || read(graph.tail_o) != tail || rtl.tail_o != tail ||
            uint64_t(cpp.nested_o_out()) != tail || read(graph.nested_o) != tail || rtl.nested_o != tail ||
            uint64_t(cpp.middle_o_out()) != middle || read(graph.middle_o) != middle || rtl.middle_o != middle)
            return 1;
        auto wide = cpp.wide_o_out();
        for (unsigned bit = 0; bit < ZERO_XLEN + 40; ++bit) {
            bool expected = bit < 32 ? ((0x89abcdefu >> bit) & 1) : ((middle >> (bit - 32)) & 1);
            // wide_o is >64 bits in the configurations below, hence VlWide.
            if (wide.get(bit) != expected || ((graph.wide_o[bit / 32] >> (bit % 32)) & 1) != expected ||
                ((rtl.wide_o[bit / 32] >> (bit % 32)) & 1) != expected)
                return 2;
        }
    }
    std::printf("zero replication: XLEN=%u VLEN=%u, 2048 C++/graph/Verilator samples passed\n", ZERO_XLEN, ZERO_VLEN);
}
