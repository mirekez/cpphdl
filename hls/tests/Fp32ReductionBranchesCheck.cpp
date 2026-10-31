// The same transactions check both preserved branches and, when available,
// the independently synthesized/retimed HLS graph.
#define cpphdl_native hardware_native
#include "hardware.h"
#undef cpphdl_native
#define cpphdl_native simulation_native
#include "replacement.h"
#undef cpphdl_native
#ifdef RETIMED_REFERENCE
#define cpphdl_native reference_native
#include "reference.h"
#undef cpphdl_native
#endif
#include "../math/Fp32Bits.h"
#include <array>
#include <deque>
#include <optional>
#include <cstdio>
#include <stdexcept>

int main(){
    constexpr unsigned R=REDUCTION_ROWS,C=REDUCTION_COLS,L=REDUCTION_LATENCY,W=R*C+1;
    constexpr unsigned Clocks=8192;
    hardware_native::Model hardware;simulation_native::Model simulation;
#ifdef RETIMED_REFERENCE
    reference_native::Model reference;
#endif
    using Result=std::array<uint32_t,R+1>;
    std::deque<std::optional<Result>> expected(L);
    constexpr uint32_t edges[]={0,0x80000000,1,0x807fffff,0x00800000,0x00800001,0x00800002,
        0x3f7fffff,0x3f7ffffe,0x3f800001,0x7f7fffff,0xff7fffff,0x3f800000,0xbf800000,
        0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xffc00000,0x80800000};
    uint32_t rng=0x83ab1457;auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    unsigned completed=0;
    for(unsigned cycle=0;cycle<Clocks;++cycle){
        bool reset=cycle<3||cycle%997==0,valid=cycle<Clocks-300&&cycle%7!=1;
        bool ready=cycle%211>=53&&cycle%11!=3;
        std::array<uint32_t,W> in{};
        for(unsigned i=0;i<R*C;++i)in[i]=cycle%3==0?edges[next()%20]:next();
        if(cycle<400)for(unsigned r=0;r<R;++r){
            for(unsigned c=0;c<C;++c)in[r*C+c]=c==0?edges[cycle%20]:c==1?edges[cycle/20]:0;
        }
        // Alternating large cancellation and small terms exposes reassociation.
        if(C>=3&&cycle%13==0)for(unsigned r=0;r<R;++r){
            in[r*C]=0x4b800000;in[r*C+1]=0xcb800000;in[r*C+2]=0x3f800000;}
        in[R*C]=cycle;
        auto drive=[&](auto& g){g.reset[0]=reset;g.command_valid_in[0]=valid;g.response_ready_in[0]=ready;
            for(unsigned i=0;i<W;++i){g.operation_in[i]=in[i];g.index_in[i]=next();g.value_in[i]=next();}
            if constexpr(requires{g.clk;}){g.clk[0]=0;g.step();}else g.eval(false);};
        drive(hardware);drive(simulation);
#ifdef RETIMED_REFERENCE
        drive(reference);
#endif
        bool outvalid=expected.back().has_value(),advance=!outvalid||ready;
        if(cycle>2&&!reset){
            auto check=[&](auto& g,const char* name){
                if(bool(g.command_ready_out[0])!=advance||bool(g.response_valid_out[0])!=outvalid||g.fault_out[0]){
                    std::fprintf(stderr,"%s handshake cycle=%u ready=%u/%u valid=%u/%u\n",name,cycle,unsigned(g.command_ready_out[0]),advance,unsigned(g.response_valid_out[0]),outvalid);
                    throw std::runtime_error("reduction latency/handshake");}
                if(outvalid)for(unsigned i=0;i<R+1;++i)if(g.result_out[i]!=(*expected.back())[i]){
                    std::fprintf(stderr,"%s cycle=%u word=%u got=%08x expected=%08x\n",name,cycle,i,g.result_out[i],(*expected.back())[i]);
                    throw std::runtime_error("reduction FP32/tree/tag mismatch");}
            };
            check(hardware,"SYNTHESIS");check(simulation,"real");
#ifdef RETIMED_REFERENCE
            check(reference,"retimed HLS");
#endif
            if(outvalid&&ready)++completed;
        }
        if(reset)expected.assign(L,std::nullopt);
        else if(advance){expected.pop_back();std::optional<Result> out;
            if(valid){out.emplace();(*out)[R]=cycle;
                for(unsigned r=0;r<R;++r){std::array<uint32_t,C> v{};
                    for(unsigned c=0;c<C;++c)v[c]=in[r*C+c];
                    for(unsigned width=C;width>1;width=(width+1)/2)
                        for(unsigned c=0;c<(width+1)/2;++c)v[c]=2*c+1<width?cpphdl::hls::math::fp32_add(v[2*c],v[2*c+1]):v[2*c];
                    (*out)[r]=cpphdl::hls::math::fp32_add(0,v[0]);}}
            expected.push_front(out);
        }
        auto edge=[&](auto& g){if constexpr(requires{g.clk;}){g.clk[0]=1;g.step();g.step();g.clk[0]=0;g.step();}else g.step();};
        edge(hardware);edge(simulation);
#ifdef RETIMED_REFERENCE
        edge(reference);
#endif
    }
    if(completed<1000)throw std::runtime_error("insufficient reduction transactions");
    std::printf("PASS reduction branches: %ux%u latency=%u, %u clocks, %u completed; FP32 tree, tags, reset, stalls, duplicate edges\n",R,C,L,Clocks,completed);
}
