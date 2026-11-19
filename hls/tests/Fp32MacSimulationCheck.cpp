#include "VFp32MacSimulation.h"
#include "../../lib/math/Fp32Bits.h"
#include <array>
#include <cstdio>
#include <cstdint>

int main() {
    VFp32MacSimulation d;d.clk=0;d.reset=1;d.en_in=0;d.a_in=0;d.b_in=0;d.c_in=0;
    constexpr uint32_t edges[]={0,0x80000000,1,0x807fffff,0x00800000,0x00800001,0x00800002,
        0x3f7fffff,0x3f7ffffe,0x3f800001,0x7f7fffff,0xff7fffff,0x3f800000,0xbf800000,
        0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xffc00000,0x80800000};
    uint32_t rng=0x18475bad;auto next=[&]{rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    std::array<uint32_t,2> expected{};
    for(unsigned cycle=0;cycle<100000;++cycle) {
        d.reset=cycle==0||cycle%997==0;d.en_in=cycle%11!=3&&cycle%211>=53;
        d.a_in=cycle<8000?edges[cycle%20]:next();
        d.b_in=cycle<8000?edges[(cycle/20)%20]:next();
        d.c_in=cycle<8000?edges[(cycle/400)%20]:next();
        d.eval();
        if(d.reset)expected={};
        else if(d.en_in) {
            expected[1]=expected[0];
            expected[0]=cpphdl_examples::math::fp32_add(d.c_in,
                cpphdl_examples::math::fp32_multiply(d.a_in,d.b_in));
        }
        d.clk=1;d.eval();d.clk=0;d.eval();
        if(d.result_out!=expected[1]) {
            std::fprintf(stderr,"FAIL Verilator fast MAC cycle=%u a=%08x b=%08x c=%08x got=%08x expected=%08x\n",
                         cycle,d.a_in,d.b_in,d.c_in,d.result_out,expected[1]);return 1;
        }
    }
    std::puts("PASS Verilator fast MAC: 100000 clocks, IEEE edge/random bits, exact hardware FP32 contract, reset/enables");
}
