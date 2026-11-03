#include <cassert>
#include <cstdio>
#ifdef USE_VERILATOR
#include "Vnonansi_ports.h"
using Model = Vnonansi_ports;
#else
#include "generated/nonansi_ports.h"
using Model = nonansi_ports;
long _system_clock = 0;
#endif

int main()
{
    Model model;
#ifndef USE_VERILATOR
    cpphdl::logic<1> write_enable = 0;
    cpphdl::logic<4> read_address = 0;
    cpphdl::logic<13> write_data = 0;
    model.we_in = _ASSIGN(write_enable);
    model.addr_in = _ASSIGN(read_address);
    model.data_in = _ASSIGN(write_data);
    model._assign();
#endif
    for (unsigned step = 0; step < 32; ++step) {
        const unsigned address = step % 16;
        const unsigned value = 0x1000 + address * 17;
#ifdef USE_VERILATOR
        model.we = step < 16;
        model.addr = address;
        model.data = value;
        model.clk = 0; model.eval();
        assert(model.live == ((step < 16 ? value : address) ^ 0x155));
        model.clk = 1; model.eval();
        model.clk = 0; model.eval();
        const unsigned wide = model.wide, narrow = model.narrow;
#else
        write_enable = step < 16;
        read_address = address;
        write_data = value;
        ++_system_clock;
        // Explicit sensitivity lists must respond without advancing a clock.
        assert(uint64_t(model.live_out()) == ((step < 16 ? value : address) ^ 0x155));
        model._work(false); model._strobe();
        ++_system_clock;
        const unsigned wide = uint64_t(model.wide_out());
        const unsigned narrow = uint64_t(model.narrow_out());
#endif
        if (step >= 16) {
            assert(wide == value);
            assert(narrow == ((0x1000 + (12 + address % 4) * 17) & 255));
            std::printf("%u %u %u\n", address, wide, narrow);
        }
    }
}
