#include "generated/DeclaredRanges.h"
#include <cstdio>
#ifdef RANGE_GRAPH
#include "model.h"
#endif
#ifdef RANGE_VERILATOR
#include "VDeclaredRanges.h"
#endif

long _system_clock = 0;

int main() {
    DeclaredRanges<TEST_DEPTH, TEST_LEFT, TEST_RIGHT> dut;
    cpphdl::logic<32> data;
    cpphdl::logic<1> reset;
    dut.data_i_in = _ASSIGN(data);
    dut.rst_ni_in = _ASSIGN(reset);
    dut._assign();
    unsigned logarithm = 0;
    for (unsigned value = TEST_DEPTH - 1; value; value >>= 1) ++logarithm;
    const unsigned width = logarithm ? logarithm : 2;
    const unsigned generalWidth = (TEST_LEFT >= TEST_RIGHT ? TEST_LEFT - TEST_RIGHT : TEST_RIGHT - TEST_LEFT) + 1;
#ifdef RANGE_GRAPH
    cpphdl_native::Model model;
#endif
#ifdef RANGE_VERILATOR
    VDeclaredRanges rtl;
#endif
    for (unsigned sample = 0; sample < 256; ++sample) {
        const uint32_t input = sample == 255 ? ~0u : sample;
        data = input;
        const bool running = sample % 17 != 0;
        reset = running;
        ++_system_clock;
        dut._work(!running);
        dut._strobe();
        ++_system_clock;
        const uint64_t expected[] = {input & ((1ull << width) - 1),
            input & ((1ull << width) - 1), input & ((1ull << generalWidth) - 1), input & 3u,
            width, generalWidth, 2 * width, running ? input & ((1ull << width) - 1) : 0,
            running ? input & ((1ull << (2 * width)) - 1) : 0};
        const uint64_t actual[] = {uint64_t(dut.data_o_out()), uint64_t(dut.ascending_o_out()),
            uint64_t(dut.general_o_out()), uint64_t(dut.literal_o_out()), uint64_t(dut.width_o_out()),
            uint64_t(dut.general_width_o_out()), uint64_t(dut.matrix_width_o_out()),
            uint64_t(dut.registered_o_out()), uint64_t(dut.matrix_o_out())};
#ifdef RANGE_GRAPH
        model.data_i[0] = input;
        model.rst_ni[0] = running;
        model.step();
        const uint64_t other[] = {model.data_o[0], model.ascending_o[0], model.general_o[0],
            model.literal_o[0], model.width_o[0], model.general_width_o[0], model.matrix_width_o[0],
            model.registered_o[0], model.matrix_o[0]};
#elif defined(RANGE_VERILATOR)
        rtl.data_i = input;
        rtl.rst_ni = running;
        rtl.clk_i = 0;
        rtl.eval();
        rtl.clk_i = 1;
        rtl.eval();
        rtl.clk_i = 0;
        rtl.eval();
        const uint64_t other[] = {rtl.data_o, rtl.ascending_o, rtl.general_o, rtl.literal_o,
            rtl.width_o, rtl.general_width_o, rtl.matrix_width_o, rtl.registered_o, rtl.matrix_o};
#else
        const auto& other = actual;
#endif
        for (unsigned port = 0; port < 9; ++port) {
            if (actual[port] != expected[port] || other[port] != expected[port]) {
                std::fprintf(stderr, "depth=%u bounds=%d:%d sample=%u port=%u expected=%llu C++=%llu other=%llu\n",
                    TEST_DEPTH, TEST_LEFT, TEST_RIGHT, sample, port, (unsigned long long)expected[port],
                    (unsigned long long)actual[port], (unsigned long long)other[port]);
                return 1;
            }
        }
    }
    std::printf("256 declared-range samples pass: depth=%u bounds=%d:%d\n", TEST_DEPTH, TEST_LEFT, TEST_RIGHT);
}
