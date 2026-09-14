#include "cpphdl.h"

template<unsigned OFFSET>
struct HlsStaticHelper
{
    static uint32_t add(uint32_t x) { return x + OFFSET; }
};

class StaticHelper : public cpphdl::Module
{
public:
    _PORT(uint32_t) value_in;
    _PORT(uint32_t) value_out;
private:
    cpphdl::reg<cpphdl::u32> result_reg;
public:
    void _assign() { value_out = _ASSIGN((uint32_t)result_reg); }
    void _work(bool reset)
    {
        if (reset) result_reg.clr();
        else result_reg._next = HlsStaticHelper<3>::add(value_in()) + HlsStaticHelper<11>::add(value_in());
    }
    void _strobe() { result_reg.strobe(); }
};

#ifndef SYNTHESIS
#include <cstdio>
#ifdef VERILATOR
#include "VStaticHelper.h"
#endif
long _system_clock = 0;
int main()
{
    uint32_t input = 0;
#ifdef VERILATOR
    VStaticHelper dut;
#else
    StaticHelper dut;
    dut.value_in = _ASSIGN(input);
    dut._assign();
#endif
    for (unsigned i = 0; i < 100; ++i) {
        input = i * 0x135790u;
#ifdef VERILATOR
        dut.clk = 0; dut.reset = i == 0; dut.value_in = input; dut.eval();
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
        const uint32_t result = dut.value_out;
#else
        dut._work(i == 0); dut._strobe(); ++_system_clock;
        const uint32_t result = dut.value_out();
#endif
        const uint32_t expected = i == 0 ? 0 : input * 2 + 14;
        if (result != expected) { std::fprintf(stderr,"static helper: %u != %u\n",result,expected); return 1; }
    }
    std::puts("PASS distinct static helper numeric specializations without --hls");
}
#endif
