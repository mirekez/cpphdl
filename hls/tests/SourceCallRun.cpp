#include "VSourceCallTop.h"
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#ifndef SOURCE_CALL_LANES
#define SOURCE_CALL_LANES 4
#endif
int main() {
    VSourceCallTop d;
    d.clk=0;d.reset=1;d.command_valid_in=0;d.response_ready_in=0;
    d.operation_in=0x1000;d.index_in=1;d.value_in=5;
    d.memory_out___05Fready_in=0;d.memory_out___05Fvalid_in=0;
    d.memory_out___05Fdata_in=7;d.memory_out___05Ferror_in=0;
    unsigned clocks=0,reads=0,replies=0,delay=9;
    std::deque<unsigned> pending;
    auto commit=[&] {
#ifdef SOURCE_CALL_RETIMED
        return bool(d.retiming_commit_out);
#else
        return true;
#endif
    };
    auto edge=[&] {
        d.clk=0;d.eval();
        bool accept=!d.reset&&commit()&&d.memory_out___05Fvalid_out&&d.memory_out___05Fready_in;
        bool reply=!d.reset&&commit()&&d.memory_out___05Fvalid_in&&d.memory_out___05Fready_out;
        if(accept&&(d.memory_out___05Fwrite_out||d.memory_out___05Faddr_out!=0x1004||d.memory_out___05Fsize_out!=4))
            throw std::runtime_error("incorrect read request");
        d.clk=1;d.eval();d.clk=0;d.eval();
        if(reply){pending.pop_front();++replies;}
        if(accept){pending.push_back(clocks+delay);++reads;}
        if(++clocks>20000)throw std::runtime_error("timeout");
    };
    struct State {bool ready,valid;uint64_t value;uint32_t fault;};
    auto step=[&](bool command,bool ready) {
        d.command_valid_in=command;d.response_ready_in=ready;
        d.memory_out___05Fready_in=(clocks%5)!=0;
        d.memory_out___05Fvalid_in=!pending.empty()&&pending.front()<=clocks;
        d.eval();
        while(!commit())edge();
        State state{bool(d.command_ready_out),bool(d.response_valid_out),d.result_out,d.fault_out};
        edge();return state;
    };
    constexpr uint64_t expected=(uint64_t((7+SOURCE_CALL_LANES-1)*5+4)<<32)|((7*5+4+SOURCE_CALL_LANES-1)*6+8);
    try {
        edge();d.reset=0;
        for(unsigned run=0;run<2;++run) {
            bool ready=false;for(unsigned i=0;i<100&&!ready;++i)ready=step(false,false).ready;
            if(!ready||!step(true,false).ready)throw std::runtime_error("command not accepted");
            State result{};
            for(unsigned i=0;i<1000&&!result.valid;++i)result=step(false,false);
            if(!result.valid||result.fault||result.value!=expected)throw std::runtime_error("source function result mismatch");
            for(unsigned i=0;i<7;++i){auto s=step(false,false);if(!s.valid||s.value!=expected)throw std::runtime_error("output changed under stall");}
            step(false,true);
            if(step(false,false).valid)throw std::runtime_error("duplicate result");
        }
        if(reads!=2||replies!=2)throw std::runtime_error("memory traffic mismatch");
        delay=1000;
        if(!step(true,false).ready)throw std::runtime_error("reset case admission failed");
        for(unsigned i=0;i<100&&reads!=3;++i)step(false,false);
        if(reads!=3||replies!=2)throw std::runtime_error("expected pending read");
        d.reset=1;pending.clear();edge();d.reset=0;
        for(unsigned i=0;i<20;++i)if(step(false,false).valid)throw std::runtime_error("reset leaked result");
        std::printf("PASS ordinary calls lanes=%u clocks=%u reads=%u replies=%u stalls/reset\n",SOURCE_CALL_LANES,clocks,reads,replies);
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL %s clocks=%u\n",e.what(),clocks);return 1;}
}
