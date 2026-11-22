#include "VWideStreamCopyTop.h"
#include <array>
#include <cstdio>
#include <deque>
#include <stdexcept>
#ifndef STREAM_BITS
#define STREAM_BITS 512
#endif
constexpr unsigned Words=STREAM_BITS/32,Bytes=STREAM_BITS/8;
using Packet=std::array<uint32_t,Words>;
int main() {
    VWideStreamCopyTop d;unsigned clocks=0,completed=0;
    d.clk=0;d.reset=1;d.command_valid_in=0;d.response_ready_in=0;
    d.memory_out___05Fvalid_in=0;d.memory_out___05Fready_in=0;d.memory_out___05Ferror_in=0;
    auto edge=[&]{d.clk=1;d.eval();d.clk=0;d.eval();++clocks;};
    auto reset=[&]{d.reset=1;d.command_valid_in=0;d.memory_out___05Fvalid_in=0;d.eval();edge();d.reset=0;d.eval();};
    try {
        reset();
        for(unsigned error=0;error<3;++error)for(unsigned count=0;count<=5;++count)
        for(unsigned selection=0;selection<4;++selection) {
            std::array<Packet,2> memory{};
            for(unsigned i=0;i<2;++i)for(unsigned j=0;j<Words;++j)memory[i][j]=0xf0000100u+(i*Words+j)*7919;
            uint32_t expected=0;
            for(unsigned round=0;round<count;++round)for(unsigned lane=0;lane<Words;++lane)
                expected+=memory[selection&1][lane]^(memory[(selection>>1)&1][lane]+round);
            struct Reply {Packet data;unsigned due;bool error;};std::deque<Reply> pending;
            bool sent=false,finished=false,held=false,heldResult=false;
            unsigned reads=0,acknowledged=0,heldAddress=0,heldSize=0;
            uint64_t oldResult=0;uint32_t oldFault=0;
            for(unsigned tick=0;tick<100000&&!finished;++tick) {
                d.command_valid_in=!sent;d.response_ready_in=tick%37>=15;
                d.operation_in=0x1000;d.index_in=count;d.value_in=selection;
                d.memory_out___05Fready_in=tick%9>=3;
                bool due=!pending.empty()&&pending.front().due<=clocks;
                d.memory_out___05Fvalid_in=due;d.memory_out___05Ferror_in=due&&pending.front().error;
                for(unsigned lane=0;lane<Words;++lane)d.memory_out___05Fdata_in[lane]=due?pending.front().data[lane]:0;
                d.eval();
                if(held&&(!d.memory_out___05Fvalid_out||d.memory_out___05Faddr_out!=heldAddress||d.memory_out___05Fsize_out!=heldSize))
                    throw std::runtime_error("wide grouped request changed under stall");
                if(heldResult&&(!d.response_valid_out||d.result_out!=oldResult||d.fault_out!=oldFault))
                    throw std::runtime_error("wide grouped result changed under stall");
                sent|=d.command_valid_in&&d.command_ready_out;
                bool request=d.memory_out___05Fvalid_out&&d.memory_out___05Fready_in;
                bool reply=due&&d.memory_out___05Fready_out;
                Reply accepted{};
                if(request) {
                    if(reads>=2||d.memory_out___05Fwrite_out||d.memory_out___05Faddr_out!=0x1000+reads*Bytes||d.memory_out___05Fsize_out!=Bytes)
                        throw std::runtime_error("wide group setup transfer mismatch");
                    accepted={memory[reads],clocks+3+selection*9,error==reads+1};++reads;
                }
                if(d.response_valid_out) {
                    if(!sent||!pending.empty()||reads!=acknowledged||d.fault_out!=(error?5:0)||(!error&&(d.result_out!=expected||reads!=2)))
                        throw std::runtime_error("wide local read group numeric/fault/drain mismatch");
                    finished=d.response_ready_in;
                }
                held=d.memory_out___05Fvalid_out&&!d.memory_out___05Fready_in;
                heldAddress=d.memory_out___05Faddr_out;heldSize=d.memory_out___05Fsize_out;
                heldResult=d.response_valid_out&&!d.response_ready_in;oldResult=d.result_out;oldFault=d.fault_out;
                edge();
                if(reply){pending.pop_front();++acknowledged;}
                if(request)pending.push_back(accepted);
            }
            if(!finished)throw std::runtime_error("wide read group timeout");
            ++completed;
            if(completed%11==0)reset();
        }
        std::printf("PASS wide streaming local groups bits=%u calls=%u clocks=%u aliases/faults/stalls/repeated-visits\n",STREAM_BITS,completed,clocks);
    } catch(const std::exception& error){std::fprintf(stderr,"FAIL %s clocks=%u\n",error.what(),clocks);return 1;}
}
