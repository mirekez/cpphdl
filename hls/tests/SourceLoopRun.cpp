#include "VSourceLoopTop.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <vector>
#ifndef LOOP_CASE
#define LOOP_CASE 1
#endif
static constexpr unsigned Jobs=LOOP_CASE==12?81:64;
using Memory=std::array<uint8_t,256>;
struct Access {unsigned job,ordinal,address,size;bool write,error;uint64_t data,reply;};
struct Job {uint32_t value,fault=0;uint64_t result=0;unsigned count=0;};
static uint64_t load(const Memory& memory,unsigned offset,unsigned size) {
    uint64_t result=0;for(unsigned i=0;i<size;++i)result|=uint64_t(memory.at(offset+i))<<(8*i);return result;
}
static void store(Memory& memory,unsigned offset,unsigned size,uint64_t value) {
    for(unsigned i=0;i<size;++i)memory.at(offset+i)=uint8_t(value>>(8*i));
}
static uint32_t floatBits(float value){uint32_t bits;std::memcpy(&bits,&value,4);return bits;}
static float bitsFloat(uint32_t bits){float value;std::memcpy(&value,&bits,4);return value;}
#include "SourceLoopReference.h"
static Memory initial() {
    Memory memory{};auto words=loopInitial();
    for(unsigned i=0;i<words.size();++i)store(memory,4*i,4,words[i]);
    return memory;
}
static Job reference(unsigned id,Memory& memory,std::deque<Access>& trace,bool errors) {
    Job job;job.value=loopValue(id);
    struct Fault {};
    auto access=[&](bool write,unsigned index,uint32_t data=0) {
        ++job.count;bool error=errors&&job.count==1+id%7;
        uint32_t reply=write?0:uint32_t(load(memory,4*index,4));
        trace.push_back({id,job.count,0x1000+4*index,4,write,error,data,reply});
        if(error)throw Fault{};
        if(write)store(memory,4*index,4,data);
        return reply;
    };
    try {
        job.result=loopReference(id%9,job.value,[&](unsigned index){return access(false,index);},
                                 [&](unsigned index,uint32_t value){access(true,index,value);});
    } catch(const Fault&){job.fault=5;}
    return job;
}
int main() {
    VSourceLoopTop d;unsigned clocks=0,completed=0,totalReads=0,totalWrites=0,peakPending=0;
    d.clk=0;d.reset=1;d.command_valid_in=0;d.response_ready_in=0;
    d.operation_in=0x1000;d.index_in=0;d.value_in=0;
    d.memory_out___05Fready_in=0;d.memory_out___05Fvalid_in=0;d.memory_out___05Fdata_in=0;d.memory_out___05Ferror_in=0;
    auto edge=[&]{d.clk=1;d.eval();d.clk=0;d.eval();++clocks;};
    auto reset=[&] {
        d.reset=1;d.command_valid_in=0;d.memory_out___05Fvalid_in=0;d.eval();edge();d.reset=0;d.eval();
        for(unsigned i=0;i<8;++i){if(d.memory_out___05Fvalid_out||d.response_valid_out)throw std::runtime_error("reset left an old access or result");edge();}
    };
    try {
        reset();
        for(unsigned campaign=0;campaign<3;++campaign) {
            bool cancel=campaign==0,errors=campaign==2;
            Memory memory=initial(),expected=memory;
            std::deque<Access> trace;std::vector<Job> jobs;
            for(unsigned id=0;id<Jobs;++id)jobs.push_back(reference(id,expected,trace,errors));
            std::array<unsigned,Jobs> acknowledged{},requested{};
            struct Pending {unsigned due;Access access;uint64_t data;};std::deque<Pending> pending;
            unsigned sent=0,received=0;
            bool heldRequest=false,heldResult=false;
            uint32_t heldAddress=0,heldSize=0,heldFault=0;bool heldWrite=false;
            uint64_t heldData=0,heldValue=0;
            for(unsigned tick=0;tick<250000&&received<Jobs;++tick) {
                d.command_valid_in=sent<Jobs;d.operation_in=0x1000;d.index_in=sent%9;d.value_in=jobs[sent<Jobs?sent:0].value;
                d.response_ready_in=tick%257>=110&&tick%19>=7;
                d.memory_out___05Fready_in=tick%17>=8;
                bool valid=!pending.empty()&&pending.front().due<=clocks;
                d.memory_out___05Fvalid_in=valid;d.memory_out___05Fdata_in=valid?pending.front().data:0;
                d.memory_out___05Ferror_in=valid&&pending.front().access.error;d.eval();
                if(heldRequest&&(!d.memory_out___05Fvalid_out||d.memory_out___05Faddr_out!=heldAddress||
                    d.memory_out___05Fsize_out!=heldSize||d.memory_out___05Fwrite_out!=heldWrite||d.memory_out___05Fdata_out!=heldData))
                    throw std::runtime_error("write request changed under backpressure");
                if(heldResult&&(!d.response_valid_out||d.result_out!=heldValue||d.fault_out!=heldFault))
                    throw std::runtime_error("source result changed under backpressure");
                bool take=d.command_valid_in&&d.command_ready_out, give=d.response_valid_out&&d.response_ready_in;
                bool request=d.memory_out___05Fvalid_out&&d.memory_out___05Fready_in,reply=valid&&d.memory_out___05Fready_out;
                if(take)++sent;
                Access access{};uint64_t readData=0;
                if(request) {
                    // Only the initial independent external-to-local copy may
                    // have read ahead of its first error. Accept an exact,
                    // ordered suffix of that loop's reads, never a store or
                    // a read from the following calculation/next invocation.
                    constexpr bool copyCase=LOOP_CASE==6 || LOOP_CASE==7 || LOOP_CASE==8 ||
                        LOOP_CASE==18 || LOOP_CASE==19 || LOOP_CASE==20 || LOOP_CASE==23;
                    unsigned copyCount=(LOOP_CASE==18 || LOOP_CASE==19)?8:received%9;
                    bool suffix=copyCase && received<sent && jobs[received].fault &&
                        jobs[received].count<=copyCount && requested[received]>=jobs[received].count &&
                        (trace.empty() || trace.front().job!=received);
                    if(suffix) {
                        if(requested[received]>=copyCount)throw std::runtime_error("speculative read passed copy loop boundary");
                        access={received,requested[received]+1,0x1000+4*requested[received],4,false,false,0,
                            load(memory,4*requested[received],4)};
                    } else {
                        if(trace.empty())throw std::runtime_error("extra external effect");
                        access=trace.front();trace.pop_front();
                    }
                    if(access.job>=sent||d.memory_out___05Faddr_out!=access.address||d.memory_out___05Fsize_out!=access.size||
                        bool(d.memory_out___05Fwrite_out)!=access.write||(access.write&&d.memory_out___05Fdata_out!=access.data)) {
                        std::fprintf(stderr,"access job=%u ordinal=%u actual=%x/%u/%u/%llx expected=%x/%u/%u/%llx\n",access.job,access.ordinal,
                            d.memory_out___05Faddr_out,d.memory_out___05Fsize_out,d.memory_out___05Fwrite_out,(unsigned long long)d.memory_out___05Fdata_out,
                            access.address,access.size,access.write,(unsigned long long)access.data);
                        throw std::runtime_error("source access order, alias dependency or store payload mismatch");
                    }
                    if(!access.write) {
                        readData=load(memory,access.address-0x1000,access.size);
                        if(!access.error&&readData!=access.reply)throw std::runtime_error("load issued before prior store became visible");
                    }
                    ++requested[access.job];
                }
                if(d.response_valid_out) {
                    if(received>=sent||acknowledged[received]!=requested[received]||requested[received]<jobs[received].count)
                        throw std::runtime_error("function returned before its memory acknowledgements");
                    if(d.fault_out!=jobs[received].fault||(!d.fault_out&&d.result_out!=jobs[received].result)) {
                        std::fprintf(stderr,"result job=%u actual=%llx/%u expected=%llx/%u\n",received,(unsigned long long)d.result_out,
                            d.fault_out,(unsigned long long)jobs[received].result,jobs[received].fault);
                        throw std::runtime_error("source return value or memory fault mismatch");
                    }
                }
                if(give){++received;if(!cancel)++completed;}
                heldRequest=d.memory_out___05Fvalid_out&&!d.memory_out___05Fready_in;heldAddress=d.memory_out___05Faddr_out;
                heldSize=d.memory_out___05Fsize_out;heldWrite=d.memory_out___05Fwrite_out;heldData=d.memory_out___05Fdata_out;
                heldResult=d.response_valid_out&&!d.response_ready_in;heldValue=d.result_out;heldFault=d.fault_out;
                edge();
                if(reply) {
                    const auto& a=pending.front().access;
                    // Writes become visible only on their delayed acknowledgement.
                    if(a.write&&!a.error)store(memory,a.address-0x1000,a.size,a.data);
                    ++acknowledged[a.job];pending.pop_front();
                }
                if(request) {
                    pending.push_back({clocks+5+(access.job+access.ordinal)%13,access,readData});
                    if(!cancel){if(access.write)++totalWrites;else ++totalReads;}
                }
                if(pending.size()>peakPending)peakPending=pending.size();
                if(cancel&&tick>120&&!pending.empty())break;
            }
            if(!cancel&&(sent!=Jobs||received!=Jobs||!trace.empty()||!pending.empty()||memory!=expected))
                throw std::runtime_error("loop effects failed to drain or final memory differs");
            reset();
        }
        std::printf("PASS source loops case=%u completed=%u reads=%u writes=%u outstanding=%u clocks=%u aliases/acknowledgements/faults/stalls/reset\n",
                    LOOP_CASE,completed,totalReads,totalWrites,peakPending,clocks);
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s clocks=%u\n",e.what(),clocks);return 1;}
}
