#include <array>
#include <cstdint>
#include <cstdio>
#if defined(RAM_GRAPH)
#include "model.h"
#elif defined(RAM_RTL)
#include "VOddWidthRam.h"
#else
#include "generated/TypedefRam.h"
long _system_clock = 0;
#endif

int main() {
#if defined(RAM_GRAPH)
    cpphdl_native::Model model;
#elif defined(RAM_RTL)
    VOddWidthRam model;
#else
    OddWidthRam model;
    cpphdl::logic<1> select, write;
    cpphdl::logic<4> address;
    cpphdl::array<1, cpphdl::logic<13>, true> data;
    model.cs_in = _ASSIGN(select);
    model.we_in = _ASSIGN(write);
    model.addr_in = _ASSIGN(address);
    model.wdata_in = _ASSIGN(data);
    model._assign();
#endif
    std::array<unsigned, 16> memory{};
    unsigned expected = 0;
    uint64_t random = 0x7498381ba34f0927ull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        const bool selected = sample < 16 || (random >> 20) & 1;
        const bool writing = sample < 16 || (random >> 21) & 1;
        const unsigned location = sample < 16 ? sample : random & 15;
        const unsigned value = sample < 16 ? 0 : (random >> 24) & 8191;
        if (selected && writing) memory[location] = value;
        if (selected && !writing) expected = memory[location];
        unsigned observed;
#if defined(RAM_GRAPH)
        model.cs[0] = selected; model.we[0] = writing; model.addr[0] = location; model.wdata[0] = value;
        model.eval(false); model.step();
        observed = model.rdata[0];
#elif defined(RAM_RTL)
        model.cs = selected; model.we = writing; model.addr = location; model.wdata = value;
        model.clk = 0; model.eval(); model.clk = 1; model.eval();
        observed = model.rdata;
#else
        select = selected; write = writing; address = location; data[0] = value;
        ++_system_clock; model._work(false); model._strobe(); ++_system_clock;
        observed = uint64_t(model.rdata_out()[0]);
#endif
        if (observed != expected) {
            std::fprintf(stderr, "13-bit RAM mismatch sample=%u expected=%u observed=%u\n", sample, expected, observed);
            return 1;
        }
    }
    std::puts("13-bit typedef RAM: 4096 oracle samples PASS");
}
