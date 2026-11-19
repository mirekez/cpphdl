#include "../../lib/math/FpAdd.h"
#include "../../lib/math/Fp32Bits.h"
#include <deque>
#include <cstdio>
#include <stdexcept>

long _system_clock=0;
template<bool P,bool N,bool A> void check() {
    FpAdd<32,8,P,N,A> dut;
    cpphdl::logic<32> a=0,b=0;bool enable=true;
    dut.a_in=[&](){return a;};dut.b_in=[&](){return b;};dut.en_in=[&](){return enable;};dut._assign();
    constexpr unsigned latency=1+N+A;
    std::deque<uint32_t> expected(latency,0);
    uint32_t rng=51;
    auto random=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    constexpr uint32_t edges[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,
        0xff7fffff,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00000};
    for(unsigned cycle=0;cycle<8192;++cycle) {
        ++_system_clock;
        a=cycle%3?random():edges[cycle%12];b=cycle%3?random():edges[(cycle/12)%12];
        enable=cycle%7!=0;bool reset=cycle<3 || cycle%997==0;
        uint32_t value=cpphdl_examples::math::fp32_add(uint32_t(a),uint32_t(b));
        // FpAdd encodes NaNs with payload 1; the FP32 helper uses a quiet NaN.
        if((value&0x7f800000u)==0x7f800000u && (value&0x7fffffu))value=0x7f800001u;
        if(!P && uint32_t(dut.c_out())!=value){std::fprintf(stderr,"P=%d N=%d A=%d cycle=%u a=%08x b=%08x got=%08x expected=%08x\n",P,N,A,cycle,uint32_t(a),uint32_t(b),uint32_t(dut.c_out()),value);throw std::runtime_error("combinational FpAdd mismatch");}
        dut._work(reset);dut._strobe();
        if(reset)expected.assign(latency,0);
        else if(enable){expected.pop_back();expected.push_front(value);}
        if(uint32_t(dut.c_out())!=(P?expected.back():value))throw std::runtime_error("FpAdd mode/latency mismatch");
    }
}
int main() {
    check<false,false,false>();check<false,true,true>();
    check<true,false,false>();check<true,true,false>();
    check<true,false,true>();check<true,true,true>();
    std::puts("PASS FpAdd combinational and registered alignment/normalization modes");
}
