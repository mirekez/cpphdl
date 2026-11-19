#include "VSourceReadTop.h"
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#ifndef SOURCE_CALL_LANES
#define SOURCE_CALL_LANES 2
#endif

int main() {
    VSourceReadTop d;
    d.clk=0;d.reset=1;d.command_valid_in=0;d.response_ready_in=0;
    d.operation_in=0;d.index_in=0;d.value_in=0;
    d.memory_out___05Fready_in=0;d.memory_out___05Fvalid_in=0;
    d.memory_out___05Fdata_in=0;d.memory_out___05Ferror_in=0;
    unsigned clocks=0,accepted=0,completed=0,requests=0,responses=0,maximumPending=0;
    struct Reply {unsigned due,id;uint32_t data;bool error;};
    struct Result {uint64_t data;uint32_t error;};
    std::deque<Reply> pending;
    auto edge=[&]{d.clk=1;d.eval();d.clk=0;d.eval();++clocks;};
    auto reset=[&] {
        d.reset=1;d.command_valid_in=0;d.memory_out___05Fvalid_in=0;d.eval();edge();
        pending.clear();d.reset=0;d.eval();
    };
    auto expected=[](uint32_t seed,uint32_t index,uint32_t value) {
        uint32_t first=(seed+SOURCE_CALL_LANES-1)*value+index+3;
        uint32_t second=(seed*value+index+3+SOURCE_CALL_LANES-1)*(value+1)+index+7;
        return (uint64_t(first)<<32)|second;
    };
    try {
        reset();
        // Clear both endpoints with a real DRAM request awaiting its answer.
        d.command_valid_in=1;d.operation_in=0x1000;d.value_in=1;
        d.memory_out___05Fready_in=1;d.eval();
        bool admitted=false,issued=false;
        for(unsigned i=0;i<100&&!issued;++i) {
            bool take=d.command_valid_in&&d.command_ready_out;
            issued=d.memory_out___05Fvalid_out;
            edge();
            if(take){admitted=true;d.command_valid_in=0;d.eval();}
        }
        if(!admitted||!issued)throw std::runtime_error("reset scenario did not issue a request");
        reset();
        for(bool stalls:{false,true}) {
            unsigned sent=0,received=0,inputRun=0,outputRun=0,maxInput=0,maxOutput=0,expectedRequest=0;
            std::deque<Result> queue;
            bool held=false;Result heldResult{};
            for(unsigned tick=0;tick<4000 && received<48;++tick) {
                uint32_t address=0x1000+sent*64,index=sent%11,value=1+sent%7;
                uint32_t seed=(address+4*index)*13u+7u;
                d.command_valid_in=sent<48;d.operation_in=address;d.index_in=index;d.value_in=value;
                d.response_ready_in=!stalls || tick%19>=7;
                d.memory_out___05Fready_in=!stalls || tick%7!=0;
                bool memoryValid=!pending.empty()&&pending.front().due<=clocks;
                d.memory_out___05Fvalid_in=memoryValid;
                d.memory_out___05Fdata_in=memoryValid?pending.front().data:0;
                d.memory_out___05Ferror_in=memoryValid&&pending.front().error;
                d.eval();
                if(held && (!d.response_valid_out || d.result_out!=heldResult.data || d.fault_out!=heldResult.error))
                    throw std::runtime_error("memory continuation output changed under backpressure");
                bool take=d.command_valid_in&&d.command_ready_out;
                bool give=d.response_valid_out&&d.response_ready_in;
                bool request=d.memory_out___05Fvalid_out&&d.memory_out___05Fready_in;
                bool reply=d.memory_out___05Fvalid_in&&d.memory_out___05Fready_out;
                if(take) {
                    queue.push_back({expected(seed,index,value),stalls&&sent%13==0?5u:0u});
                    ++sent;++accepted;
                }
                if(give) {
                    if(queue.empty() || d.fault_out!=queue.front().error ||
                       (!d.fault_out && d.result_out!=queue.front().data))
                        throw std::runtime_error("memory continuation lost, reordered or corrupted a job");
                    queue.pop_front();++received;++completed;
                }
                uint32_t readAddress=d.memory_out___05Faddr_out;
                if(request && (d.memory_out___05Fwrite_out || d.memory_out___05Fsize_out!=4 ||
                    readAddress!=0x1000+expectedRequest*64+4*(expectedRequest%11)))
                    throw std::runtime_error("memory request changed or arrived out of order");
                held=d.response_valid_out&&!d.response_ready_in;heldResult={d.result_out,d.fault_out};
                edge();
                if(reply){pending.pop_front();++responses;}
                if(request) {
                    pending.push_back({clocks+(stalls?3+expectedRequest%7:3),expectedRequest,
                        readAddress*13u+7u,stalls&&expectedRequest%13==0});
                    ++expectedRequest;++requests;
                }
                if(pending.size()>maximumPending)maximumPending=pending.size();
                inputRun=take?inputRun+1:0;outputRun=give?outputRun+1:0;
                if(inputRun>maxInput)maxInput=inputRun;if(outputRun>maxOutput)maxOutput=outputRun;
            }
            if(sent!=48 || received!=48 || expectedRequest!=48 || !queue.empty() || !pending.empty())
                throw std::runtime_error("memory continuation timeout or outstanding work");
            if(!stalls && (maxInput!=48 || maxOutput!=48))
                throw std::runtime_error("memory pipeline does not sustain one transaction per clock");
            d.command_valid_in=0;d.memory_out___05Fvalid_in=0;d.response_ready_in=1;d.eval();
            for(unsigned i=0;i<8;++i){if(d.response_valid_out)throw std::runtime_error("duplicate response");edge();}
        }
        if(requests!=96 || responses!=96 || maximumPending<4 || maximumPending>8)
            throw std::runtime_error("memory request window was serialized or exceeded capacity");
        std::printf("PASS source read pipeline accepted=%u completed=%u requests=%u responses=%u outstanding=%u clocks=%u II=1 stalls/errors/reset\n",
            accepted,completed,requests,responses,maximumPending,clocks);
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL %s clocks=%u\n",e.what(),clocks);return 1;}
}
