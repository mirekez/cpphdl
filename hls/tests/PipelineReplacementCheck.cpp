#define cpphdl_native reference_native
#include "reference.h"
#undef cpphdl_native
#define cpphdl_native replacement_native
#include "replacement.h"
#undef cpphdl_native
#include <cstdint>
#include <deque>
#include <cstdio>
#include <stdexcept>
#ifdef FP32_MAC_CHECK
#include "../math/Fp32Bits.h"
#endif

int main() {
    reference_native::Model reference;
    replacement_native::Model replacement;
    std::deque<uint64_t> expected;
    uint32_t random=0x79856743;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    unsigned accepted=0,completed=0;
    auto rtlEvaluate=[&](unsigned clk){replacement.clk[0]=clk;replacement.step();};
    for(unsigned cycle=0;cycle<8192;++cycle) {
        const bool reset=cycle<3 || cycle%997==0;
        const bool valid=cycle<7800 && cycle%7!=1;
        const bool ready=cycle%211>=53 && cycle%11!=3;
        uint32_t a=next(),b=next(),c=next();
        auto drive=[&](auto& g){g.reset[0]=reset;g.command_valid_in[0]=valid;
            g.response_ready_in[0]=ready;g.operation_in[0]=a;g.index_in[0]=b;g.value_in[0]=c;};
        drive(reference);drive(replacement);reference.eval(false);rtlEvaluate(0);
        if(cycle>2 && !reset) {
            if(reference.command_ready_out[0]!=replacement.command_ready_out[0] ||
               reference.response_valid_out[0]!=replacement.response_valid_out[0])
                throw std::runtime_error("replacement latency/handshake mismatch");
            if(replacement.response_valid_out[0]) {
                auto result=uint64_t(replacement.result_out[0]);
                auto original=uint64_t(reference.result_out[0]);
#ifndef FP32_MAC_CHECK
                result|=uint64_t(replacement.result_out[1])<<32;
                original|=uint64_t(reference.result_out[1])<<32;
#endif
                if(expected.empty() || result!=original || result!=expected.front() || replacement.fault_out[0])
                    throw std::runtime_error("replacement result/tag/rounding mismatch");
                if(ready){expected.pop_front();++completed;}
            }
            if(valid && replacement.command_ready_out[0]) {
#ifdef FP32_MAC_CHECK
                expected.push_back(cpphdl::hls::math::fp32_add(a,cpphdl::hls::math::fp32_multiply(b,c)));++accepted;
#else
                expected.push_back((uint64_t(b)<<32)|uint32_t(a*c+b));++accepted;
#endif
            }
        }
        if(reset)expected.clear();
        reference.step();rtlEvaluate(1);rtlEvaluate(0);
    }
    if(!expected.empty() || completed<1000) throw std::runtime_error("incomplete replacement regression");
    std::printf("PASS replacement: 8192 clocks, %u accepted, %u completed; resets, bubbles, stalls, tags\n",accepted,completed);
}
