#include "../../Clocked.h"
#include <cstring>
#include <deque>
#include <cstdio>
#include <stdexcept>
namespace cpphdl::hls::math {
template<class T,unsigned Rows,unsigned Cols> struct TestPacket {
    T initial[Rows][Cols]; // SV keyword: declaration and access must agree.
    T tag;
};
}
using Packet=cpphdl::hls::math::TestPacket<uint32_t,2,3>;
struct WideMethods {
    Packet command(Packet input,Packet,Packet) const {
        Packet result{};result.tag=input.tag;
#pragma omp simd collapse(2)
        for(int row=0;row<2;++row)for(int col=0;col<3;++col)
            result.initial[row][col]=input.initial[row][col]*3u+input.tag;
        return result;
    }
};
class PipelineTop:public cpphdl::Module {
public:
    using PacketArray=Packet[2]; // Preserve unpacked dimensions in the typedef.
    cpphdl::hls::ClockedPipeline<WideMethods,3,Packet,Packet> worker[2];
    _PORT(Packet) input_in;
    _PORT(bool) valid_in;
    _PORT(bool) ready_in;
    _PORT(bool) ready_out;
    _PORT(bool) valid_out;
    _PORT(Packet) first_out;
    _PORT(Packet) second_out;
    void _assign(){
        for(uint32_t i=0;i<2;++i){
            worker[i].operation_in=_ASSIGN(input_in());worker[i].index_in=_ASSIGN(input_in());worker[i].value_in=_ASSIGN(input_in());
            worker[i].command_valid_in=_ASSIGN(valid_in());worker[i].response_ready_in=_ASSIGN(ready_in());worker[i]._assign();
        }
        ready_out=_ASSIGN(worker[0].command_ready_out());valid_out=_ASSIGN(worker[0].response_valid_out());
        first_out=_ASSIGN(worker[0].result_out());second_out=_ASSIGN(worker[1].result_out());
    }
    void _work(bool reset){for(uint32_t i=0;i<2;++i)worker[i]._work(reset);}
    void _strobe(){for(uint32_t i=0;i<2;++i)worker[i]._strobe();}
};
#ifndef SYNTHESIS
#ifdef VERILATOR
#include "VPipelineTop.h"
#endif
#ifndef PIPELINE_LATENCY
#define PIPELINE_LATENCY 3
#endif
long _system_clock=0;
int main(){try{
#ifdef VERILATOR
    VPipelineTop dut;
#else
    PipelineTop dut;
#endif
    Packet input{};bool valid=false,ready=true,blocked=false;
#ifndef VERILATOR
    dut.input_in=_ASSIGN(input);dut.valid_in=_ASSIGN(valid);dut.ready_in=_ASSIGN(ready);dut._assign();
#endif
    std::deque<Packet> pending;unsigned accepted=0,delivered=0,cancelled=0;
    auto cycle=[&](bool reset,unsigned time){
#ifdef VERILATOR
        dut.clk=0;dut.reset=reset;dut.valid_in=valid;dut.ready_in=ready;
        std::memcpy(dut.input_in.data(),&input,sizeof(input));dut.eval();
        bool cr=dut.ready_out,rv=dut.valid_out;Packet first{},second{};
        std::memcpy(&first,dut.first_out.data(),sizeof(first));std::memcpy(&second,dut.second_out.data(),sizeof(second));
#else
        bool cr=dut.ready_out(),rv=dut.valid_out();Packet first=dut.first_out(),second=dut.second_out();
#endif
        blocked=valid&&!cr&&!reset;
        if(reset){cancelled+=pending.size();pending.clear();}
        else{
            if(valid&&cr){Packet expected{};expected.tag=input.tag;
                for(unsigned r=0;r<2;++r)for(unsigned c=0;c<3;++c)expected.initial[r][c]=input.initial[r][c]*3u+input.tag;
                pending.push_back(expected);++accepted;}
            if(rv){if(pending.empty()||std::memcmp(&first,&pending.front(),sizeof(first))||std::memcmp(&second,&first,sizeof(first)))throw std::runtime_error("wide packet/tag/worker mismatch");
                if(ready){pending.pop_front();++delivered;}}
            if(time<=128&&(!cr||(time>PIPELINE_LATENCY&&!rv)))throw std::runtime_error("wide stream bubble");
        }
#ifdef VERILATOR
        dut.clk=1;dut.eval();dut.clk=0;dut.eval();
#else
        dut._work(reset);dut._strobe();
#endif
        ++_system_clock;
    };
    cycle(true,0);
    for(unsigned t=1;t<900;++t){
        if(!blocked){input.tag=t;valid=t<=128||t%7;
            for(unsigned r=0;r<2;++r)for(unsigned c=0;c<3;++c)input.initial[r][c]=t*123456789u+r*7+c;}
        ready=t<=128||t%29<19;cycle(t==599,t);
    }
    valid=false;ready=true;for(unsigned t=0;t<=PIPELINE_LATENCY;++t)cycle(false,1000+t);
    if(!pending.empty()||accepted!=delivered+cancelled)throw std::runtime_error("wide credit conservation");
    std::printf("Wide records: 224 bits, two workers, II=1, %u stages, %u delivered; templates, keywords, typedef arrays, loops, stalls/reset passed\n",PIPELINE_LATENCY,delivered);
}catch(const std::exception& e){puts(e.what());return 1;}}
#endif
