#ifdef VERILATOR
#include "VRootInstanceTop.h"
using Device = VRootInstanceTop;
#else
#include "TemplateRootInstance.h"
long _system_clock = -1;
using Device = RootInstanceTop<8>;
#endif
#include <cstdio>
#include <stdexcept>

int main()
{
    Device dut;
    unsigned input = 0;
#ifndef VERILATOR
    dut.value_in = _ASSIGN(input);
    dut._assign();
#endif
    for (unsigned cycle = 0; cycle < 600; ++cycle) {
        input = (cycle * 37) & 255;
        const bool reset = cycle % 53 == 0;
#ifdef VERILATOR
        dut.value_in = input;
        dut.reset = reset;
        dut.clk = 0;
        dut.eval();
        dut.clk = 1;
        dut.eval();
        const unsigned result = dut.value_out;
#else
        dut._work(reset);
        dut._strobe();
        ++_system_clock;
        const unsigned result = uint32_t(dut.value_out());
#endif
        if (result != (reset ? 0 : (input + 3) & 255)) {
            std::fprintf(stderr, "cycle %u: got %u, expected %u\n", cycle, result,
                         reset ? 0 : (input + 3) & 255);
            throw std::runtime_error("standalone template hierarchy lost wiring or register updates");
        }
    }
    std::puts("PASS standalone template hierarchy: 600 cycles including resets and overflow");
}
