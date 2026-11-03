#include "../math/Fp32Bits.h"
#ifdef FP_MAC_NATIVE_MODEL
#include FP_MAC_NATIVE_MODEL
#else
#include "../math/Fp32Mac.h"
long _system_clock=0;
#endif
#include <array>
#include <cstdio>
#include <stdexcept>

int main() {
    using namespace cpphdl::hls::math;
#ifdef FP_MAC_NATIVE_MODEL
    cpphdl_native::Model dut;
#else
    Fp32Mac dut;
#endif
    uint32_t a=0,b=0,c=0; bool enable=true;
#ifndef FP_MAC_NATIVE_MODEL
    dut.a_in=_ASSIGN(a);dut.b_in=_ASSIGN(b);dut.c_in=_ASSIGN(c);dut.en_in=_ASSIGN(enable);dut._assign();
#endif
    std::array<uint32_t,2> expected{};
    std::array<bool,2> valid{};
    uint32_t random=0x786325ad;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    const uint32_t edge[]={0,0x80000000u,1,0x007fffffu,0x00800000u,0x00800001u,
        0x3f800000u,0x3f800001u,0xbf800000u,0x3fffffffu,0x7f7fffffu,
        0x7f800000u,0xff800000u,0x7fc00000u,0xffa12345u,0x80800000u};
    for(unsigned cycle=0;cycle<100000;++cycle) {
        bool reset=cycle==0 || cycle%997==0;
        enable=cycle%13>3;
        if(cycle<4096){a=edge[cycle&15];b=edge[(cycle>>4)&15];c=edge[(cycle>>8)&15];}
        else {a=next();b=next();c=next();}
        if(reset){expected={};valid={};}
        else if(enable){expected[1]=expected[0];expected[0]=fp32_add(c,fp32_multiply(a,b));valid[1]=valid[0];valid[0]=true;}
#ifdef FP_MAC_NATIVE_MODEL
        dut.a_in[0]=a;dut.b_in[0]=b;dut.c_in[0]=c;dut.en_in[0]=enable;dut.reset[0]=reset;
        dut.clk[0]=0;dut.step();dut.clk[0]=1;dut.step();dut.clk[0]=0;dut.step();
        uint32_t result=dut.result_out[0];
#else
        dut._work(reset);dut._strobe();++_system_clock;
        uint32_t result=dut.result_out();
#endif
        if(valid[1] && result!=expected[1]) {
            std::fprintf(stderr,"clock=%u result=%08x expected=%08x\n",cycle,result,expected[1]);
            throw std::runtime_error("FP32 MAC primitive mismatch");
        }
    }
    std::puts("PASS FP32 MAC: 100000 clocks, FP32 classes, random inputs, enable stalls and reset");
}
