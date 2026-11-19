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
    d.clk=0; d.reset=1; d.command_valid_in=0; d.response_ready_in=0;
    d.operation_in=0; d.index_in=0; d.value_in=0;
    unsigned clocks=0,accepted=0,completed=0;
    auto edge=[&] { d.clk=1;d.eval();d.clk=0;d.eval();++clocks; };
    auto reset=[&] { d.reset=1;d.command_valid_in=0;d.eval();edge();d.reset=0;d.eval(); };
    auto expected=[](uint32_t seed,uint32_t index,uint32_t value) {
        uint32_t first=(seed+SOURCE_CALL_LANES-1)*value+index+3;
        uint32_t second=(seed*value+index+3+SOURCE_CALL_LANES-1)*(value+1)+index+7;
        return (uint64_t(first)<<32)|second;
    };
    try {
        reset();
        // Reset with calculations in flight, then check that new jobs cannot
        // observe pre-reset data or valid bits from either nested call site.
        d.command_valid_in=1;d.operation_in=123;d.index_in=2;d.value_in=9;d.eval();
        for(unsigned i=0;i<3;++i)edge();
        reset();
        for(bool stalls:{false,true}) {
            unsigned sent=0,received=0,inputRun=0,outputRun=0,maxInput=0,maxOutput=0;
            bool held=false;uint64_t heldValue=0;std::deque<uint64_t> queue;
            for(unsigned tick=0;tick<2000 && received<48;++tick) {
                uint32_t seed=7+sent*13,index=sent%11,value=1+sent%7;
                d.command_valid_in=sent<48;
                d.operation_in=seed;d.index_in=index;d.value_in=value;
                d.response_ready_in=!stalls || tick%17>=9;
                d.eval();
                if(held && (!d.response_valid_out || d.result_out!=heldValue || d.fault_out))
                    throw std::runtime_error("nested source output changed under backpressure");
                bool take=d.command_valid_in&&d.command_ready_out;
                bool give=d.response_valid_out&&d.response_ready_in;
                if(take) {queue.push_back(expected(seed,index,value));++sent;++accepted;}
                if(give) {
                    if(queue.empty() || d.result_out!=queue.front() || d.fault_out)
                        throw std::runtime_error("nested source stream reordered or corrupted a result");
                    queue.pop_front();++received;++completed;
                }
                inputRun=take?inputRun+1:0;outputRun=give?outputRun+1:0;
                if(inputRun>maxInput)maxInput=inputRun;
                if(outputRun>maxOutput)maxOutput=outputRun;
                held=d.response_valid_out&&!d.response_ready_in;heldValue=d.result_out;
                edge();
            }
            if(sent!=48 || received!=48 || !queue.empty())throw std::runtime_error("source stream timeout");
            if(!stalls && (maxInput!=48 || maxOutput!=48))
                throw std::runtime_error("ordinary call pipeline does not sustain one transaction per clock");
            d.command_valid_in=0;d.response_ready_in=1;d.eval();
            for(unsigned i=0;i<5;++i) {
                if(d.response_valid_out)throw std::runtime_error("duplicate stream response");
                edge();
            }
        }
        std::printf("PASS source stream lanes=%u accepted=%u completed=%u clocks=%u II=1 stalls/reset\n",
                    SOURCE_CALL_LANES,accepted,completed,clocks);
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL %s clocks=%u\n",e.what(),clocks);return 1;}
}
