#include "VWideStreamCopyTop.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <vector>
#ifndef STREAM_BITS
#define STREAM_BITS 512
#endif
constexpr unsigned Words=STREAM_BITS/32,Bytes=STREAM_BITS/8;
using Packet=std::array<uint32_t,Words>;
int main() {
    VWideStreamCopyTop d;
    unsigned clocks=0,completed=0,peak=0,readTotal=0,writeTotal=0;
    d.clk=0;d.reset=1;d.command_valid_in=0;d.response_ready_in=0;
    d.memory_out___05Fvalid_in=0;d.memory_out___05Fready_in=0;d.memory_out___05Ferror_in=0;
    auto edge=[&]{d.clk=1;d.eval();d.clk=0;d.eval();++clocks;};
    auto reset=[&] {
        d.reset=1;d.command_valid_in=0;d.memory_out___05Fvalid_in=0;d.eval();edge();
        d.reset=0;d.eval();
        for(unsigned i=0;i<5;++i){if(d.memory_out___05Fvalid_out||d.response_valid_out)throw std::runtime_error("stale token after reset");edge();}
    };
    try {
        reset();
        // Cancel once with requests in flight, then run repeated calls without
        // reset. Both endpoints discard pending responses on cancellation.
        for(unsigned cancellation=0;cancellation<2;++cancellation)
        for(unsigned delay:{0u,3u,40u})for(unsigned count:{0u,1u,2u,4u,8u,16u})
        for(unsigned failure=0;failure<3;++failure) {
            const bool cancel=cancellation==0;
            if(cancel&&(delay!=40||count!=16||failure!=0))continue;
            std::array<uint32_t,1024> memory;memory.fill(0xdeadbeefu);
            for(unsigned i=0;i<16*Words;++i)memory[i]=0x10000+i*17+completed*101;
            struct Reply {Packet data;unsigned due,address;bool write,error;};
            std::deque<Reply> queue;
            unsigned reads=0,writes=0,replies=0;
            bool submitted=false,finished=false,held=false,heldResult=false;
            unsigned heldAddr=0,heldSize=0;bool heldWrite=false;Packet heldData{};
            uint64_t savedResult=0;uint32_t savedFault=0;
            for(unsigned tick=0;tick<100000 && !finished;++tick) {
                d.command_valid_in=!submitted;d.response_ready_in=tick%31>=9;
                d.operation_in=0x1000;d.index_in=count;d.value_in=0x1800;
                d.memory_out___05Fready_in=tick%11>=3;
                bool due=!queue.empty()&&queue.front().due<=clocks&&tick%7!=0;
                d.memory_out___05Fvalid_in=due;d.memory_out___05Ferror_in=due&&queue.front().error;
                for(unsigned i=0;i<Words;++i)d.memory_out___05Fdata_in[i]=due?queue.front().data[i]:0;
                d.eval();
                Packet data{};for(unsigned i=0;i<Words;++i)data[i]=d.memory_out___05Fdata_out[i];
                if(held&&(!d.memory_out___05Fvalid_out||d.memory_out___05Faddr_out!=heldAddr||
                   d.memory_out___05Fsize_out!=heldSize||bool(d.memory_out___05Fwrite_out)!=heldWrite||data!=heldData))
                    throw std::runtime_error("wide request changed under backpressure");
                if(heldResult&&(!d.response_valid_out||d.result_out!=savedResult||d.fault_out!=savedFault))
                    throw std::runtime_error("wide result changed under backpressure");
                submitted|=d.command_valid_in&&d.command_ready_out;
                const bool request=d.memory_out___05Fvalid_out&&d.memory_out___05Fready_in;
                const bool reply=due&&d.memory_out___05Fready_out;
                Reply accepted{};
                if(request) {
                    const bool write=d.memory_out___05Fwrite_out;
                    const auto address=d.memory_out___05Faddr_out;
                    const auto size=d.memory_out___05Fsize_out;
                    if(size!=Bytes||address%Bytes)throw std::runtime_error("wide access split or misaligned");
                    accepted={data,clocks+delay,address,write,false};
                    if(write) {
                        if(failure==1||reads!=count||writes>=std::min(count,4u)||address!=0x1800+writes*Bytes)
                            throw std::runtime_error("write issued after read fault or out of order");
                        const auto index=writes+4*((count-1-writes)/4);
                        for(unsigned lane=0;lane<Words;++lane) {
                            auto want=memory[index*Words+lane];
                            if(lane==0)want^=count;if(lane==Words-1)want+=writes;
                            if(data[lane]!=want)throw std::runtime_error("wide local data/alias order mismatch");
                        }
                        accepted.error=failure==2&&writes==0;++writes;
                    } else {
                        if(reads>=count||address!=0x1000+reads*Bytes)throw std::runtime_error("read escaped copy bounds/order");
                        for(unsigned lane=0;lane<Words;++lane)accepted.data[lane]=memory[reads*Words+lane];
                        accepted.error=failure==1&&reads==count/2;++reads;
                    }
                }
                if(d.response_valid_out) {
                    const unsigned expectedFault=count&&failure?5:0;
                    if(!submitted||!queue.empty()||d.fault_out!=expectedFault||replies!=reads+writes)
                        throw std::runtime_error("wide completion did not drain or fault mismatch");
                    if(!expectedFault&&(reads!=count||writes!=std::min(count,4u)||
                        d.result_out!=(count?memory[(count-1)*Words+Words-1]:0)))
                        throw std::runtime_error("wide successful result/traffic mismatch");
                    if(expectedFault&&failure==2&&writes!=1)throw std::runtime_error("writes continued after failed write");
                    finished=d.response_ready_in;
                }
                held=d.memory_out___05Fvalid_out&&!d.memory_out___05Fready_in;
                heldAddr=d.memory_out___05Faddr_out;heldSize=d.memory_out___05Fsize_out;heldWrite=d.memory_out___05Fwrite_out;heldData=data;
                heldResult=d.response_valid_out&&!d.response_ready_in;savedResult=d.result_out;savedFault=d.fault_out;
                edge();
                if(reply) {
                    const auto old=queue.front();queue.pop_front();++replies;
                    if(old.write&&!old.error)for(unsigned lane=0;lane<Words;++lane)memory[(old.address-0x1000)/4+lane]=old.data[lane];
                }
                if(request)queue.push_back(accepted);
                peak=std::max(peak,unsigned(queue.size()));
                if(cancel&&queue.size()>=2){reset();finished=true;break;}
            }
            if(!finished)throw std::runtime_error("wide stream timeout");
            if(!cancel){++completed;readTotal+=reads;writeTotal+=writes;}
        }
        if(peak<2)throw std::runtime_error("wide copy never overlapped reads");
        std::printf("PASS wide streaming bits=%u calls=%u reads=%u writes=%u outstanding=%u clocks=%u aliases/faults/stalls/reset\n",
            STREAM_BITS,completed,readTotal,writeTotal,peak,clocks);
    } catch(const std::exception& error){std::fprintf(stderr,"FAIL %s clocks=%u\n",error.what(),clocks);return 1;}
}
