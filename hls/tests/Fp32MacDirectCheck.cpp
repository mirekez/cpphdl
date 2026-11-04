// Direct C++RTL, direct native graph and optional RTL round-trip equivalence.
#include "../math/Fp32MacRtlPipeline.h"
#include "model.h"
#ifdef RTL_REFERENCE
#define cpphdl_native rtl_native
#include "replacement.h"
#undef cpphdl_native
#endif
#include <array>
#include <deque>
#include <optional>
#include <cstdio>
#include <stdexcept>
#ifndef MAC_ROWS
#define MAC_ROWS 4
#define MAC_COLS 4
#endif
#ifndef MAC_LATENCY
#define MAC_LATENCY 48
#endif
long _system_clock=0;
int main() {
    constexpr unsigned R=MAC_ROWS,C=MAC_COLS,N=R*C,W=2*N+R+C+4,L=MAC_LATENCY;
    Fp32MacRtlArray<R,C,L> source;
    struct Native : cpphdl_native::Model {
        decltype(command_valid)& command_valid_in=command_valid;
        decltype(response_ready)& response_ready_in=response_ready;
        decltype(operation)& operation_in=operation;
        decltype(index)& index_in=index;
        decltype(value)& value_in=value;
        decltype(command_ready)& command_ready_out=command_ready;
        decltype(response_valid)& response_valid_out=response_valid;
        decltype(result)& result_out=result;
        decltype(fault)& fault_out=fault;
        decltype(work_reset)& reset=work_reset;
    } native;
#ifdef RTL_REFERENCE
    rtl_native::Model rtl;
#endif
    bool valid=false,ready=false;
    cpphdl::logic<W*32> packet;
    source.command_valid_in=[&](){return valid;};source.response_ready_in=[&](){return ready;};
    source.operation_in=[&](){return packet;};source.index_in=[&](){return packet;};source.value_in=[&](){return packet;};
    source._assign();
    using Result=std::array<uint32_t,N+1>;
    std::deque<std::optional<Result>> expected(L);
    uint32_t rng=0x12347adf;auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    constexpr uint32_t edges[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,0xff7fffff,
        0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xffc00000};
    unsigned completed=0;
    for(unsigned cycle=0;cycle<8192;++cycle){
        ++_system_clock;
        bool reset=cycle<3 || cycle%997==0;
        valid=cycle<7800 && cycle%7!=1;ready=cycle%211>=53 && cycle%11!=3;
        std::array<uint32_t,W> input;
        for(auto& word:input)word=(cycle%3==0)?edges[next()%14]:next();
        input[W-4]=next()%(R+1);input[W-3]=next()%(C+1);input[W-2]=cycle;input[W-1]=next()%3;
        for(unsigned i=0;i<W;++i)packet.bits(i*32+31,i*32)=input[i];
        auto drive=[&](auto& g){g.reset[0]=reset;g.command_valid_in[0]=valid;g.response_ready_in[0]=ready;
            for(unsigned i=0;i<W;++i){g.operation_in[i]=input[i];g.index_in[i]=input[i];g.value_in[i]=input[i];}};
        drive(native);native.eval(false);
#ifdef RTL_REFERENCE
        drive(rtl);rtl.clk[0]=0;rtl.step();
#endif
        bool outvalid=expected.back().has_value(),advance=!outvalid||ready;
        if(cycle>2 && !reset){
            auto check=[&](auto& g,const char* label){
                if(bool(g.command_ready_out[0])!=advance || bool(g.response_valid_out[0])!=outvalid || g.fault_out[0])
                    {std::fprintf(stderr,"%s cycle=%u ready=%u/%u valid=%u/%u fault=%u\n",label,cycle,g.command_ready_out[0],advance,g.response_valid_out[0],outvalid,g.fault_out[0]);
                    throw std::runtime_error("native latency/handshake/fault");}
                if(outvalid)for(unsigned i=0;i<N+1;++i)if(g.result_out[i]!=(*expected.back())[i]){
                    std::fprintf(stderr,"cycle=%u word=%u got=%08x expected=%08x\n",cycle,i,g.result_out[i],(*expected.back())[i]);
                    throw std::runtime_error("native FP32/tag/mask");}
            };
            check(native,"direct");
#ifdef RTL_REFERENCE
            check(rtl,"rtl");
#endif
            if(source.command_ready_out()!=advance || source.response_valid_out()!=outvalid || source.fault_out())
                throw std::runtime_error("C++RTL latency/handshake/fault");
            if(outvalid){auto result=source.result_out();for(unsigned i=0;i<N+1;++i)
                if(uint32_t(result.bits(i*32+31,i*32))!=(*expected.back())[i])throw std::runtime_error("C++RTL FP32/tag/mask");
                if(ready)++completed;}
        }
        if(reset)expected.assign(L,std::nullopt);
        else if(advance){expected.pop_back();std::optional<Result> result;
            if(valid){result.emplace();(*result)[N]=cycle;
                for(unsigned r=0;r<R;++r)for(unsigned c=0;c<C;++c){unsigned i=r*C+c;auto initial=input[N+C+i];
                    (*result)[i]=r<input[W-4] && c<input[W-3] ? cpphdl_examples::math::fp32_add(initial,
                        cpphdl_examples::math::fp32_multiply(input[W-1]==0?input[2*N+C+r]:input[i],input[N+c])):initial;}}
            expected.push_front(result);
        }
        source._work(reset);source._strobe();native.step();
#ifdef RTL_REFERENCE
        rtl.clk[0]=1;rtl.step();rtl.clk[0]=0;rtl.step();
#endif
    }
    if(completed<1000)throw std::runtime_error("insufficient transactions");
    std::printf("PASS direct C++RTL/native MAC: %ux%u latency=%u, 8192 clocks, %u completed; FP32 edge cases, modes, masks, reset, stalls\n",R,C,L,completed);
}
