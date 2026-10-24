#include "DimensionTop.h"
#include <array>
#include <cassert>
#include <cstdio>
#ifdef VERILATOR
#include "VMaskedRam.h"
#endif
long _system_clock = 0;
class Bench : public cpphdl::Module
{
public:
    bool read = false, write = false;
    uint32_t ra = 0, wa = 0, mask = 0;
    cpphdl::array<2, cpphdl::u32> data{};
#ifdef VERILATOR
    VMaskedRam dut;
#else
    MaskedRam dut;
#endif
    Bench()
    {
        _assign();
    }
    void _assign()
    {
#ifndef VERILATOR
        dut.read_in = _ASSIGN(read);
        dut.write_in = _ASSIGN(write);
        dut.read_address_in = _ASSIGN(ra);
        dut.write_address_in = _ASSIGN(wa);
        dut.write_mask_in = _ASSIGN(cpphdl::logic<2>(mask));
        dut.data_in = _ASSIGN(data);
        dut._assign();
#endif
    }
    uint64_t tick(bool reset = false)
    {
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.read_in = read;
        dut.write_in = write;
        dut.read_address_in = ra;
        dut.write_address_in = wa;
        dut.write_mask_in = mask;
        dut.data_in = uint64_t(data[0]) | (uint64_t(data[1]) << 32);
        dut.eval();
        dut.clk = 1;
        dut.eval();
        ++_system_clock;
        return dut.data_out;
#else
        dut._work(reset);
        dut._strobe();
        ++_system_clock;
        return uint64_t(dut.data_out()[0]) | (uint64_t(dut.data_out()[1]) << 32);
#endif
    }
};
int main()
{
    Bench b;
    std::array<uint64_t, 8> words{};
    uint64_t output = 0;
    b.tick(true);
    for (unsigned i = 0; i < 8; ++i)
    {
        b.wa = i;
        b.mask = 3;
        b.write = true;
        b.data = 0;
        b.tick();
    }
    for (unsigned i = 0; i < 1000; ++i)
    {
        b.read = i % 3 != 0;
        b.write = i % 5 != 0;
        b.ra = (i / 7) % 8;
        b.wa = (i / 11) % 8;
        b.mask = i % 4;
        b.data[0] = i * 7919;
        b.data[1] = ~(i * 1237u);
        bool reset = i % 97 == 0;
        if (!reset)
        {
            if (b.read)
                output = words[b.ra];
            uint64_t bits = uint64_t(b.data[0]) | (uint64_t(b.data[1]) << 32),
                     mask = (b.mask & 1 ? 0xffffffffull : 0) |
                            (b.mask & 2 ? 0xffffffff00000000ull : 0);
            if (b.write)
                words[b.wa] = (words[b.wa] & ~mask) | (bits & mask);
        }
        assert(b.tick(reset) == output);
    }
    std::puts(
        "PASS masked RAM: word enables, read-before-write, held outputs and reset preservation");
}
