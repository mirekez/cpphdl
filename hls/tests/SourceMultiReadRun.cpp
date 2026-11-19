#include "VSourceMultiReadTop.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <vector>
#ifndef MULTI_READ_CASE
#define MULTI_READ_CASE 2
#endif
#ifndef SOURCE_READ_DEPTH
#define SOURCE_READ_DEPTH 8
#endif

struct Access {uint32_t address,data;bool error;};
struct Job {std::deque<Access> reads;uint64_t result;uint32_t fault=0;};
static uint32_t floatBits(float value){uint32_t bits;std::memcpy(&bits,&value,4);return bits;}
static float bitsFloat(uint32_t bits){float value;std::memcpy(&value,&bits,4);return value;}
static Job reference(unsigned id,bool errors) {
    Job job;
    uint32_t base=0x10000+id*0x1000,index=id%8,value=id;
    unsigned ordinal=0;
    auto read=[&](unsigned offset) {
        if(job.fault)return uint32_t(0);
        uint32_t address=base+4*offset,data=(address/4)*13u+7u;
#if MULTI_READ_CASE==6
        data=0x3d000000u|(((address/4)*0x9e3779b9u)&0x01ffffffu)|((offset&1)?0x80000000u:0u);
#endif
        bool error=errors&&++ordinal==id%4;
        job.reads.push_back({address,data,error});
        if(error)job.fault=5;
        return data;
    };
#if MULTI_READ_CASE==0
    auto a=read(index),b=read(index+1);job.result=(uint64_t(a)<<32)|b;
#elif MULTI_READ_CASE==1
    job.result=value&1?read(index):value+7;
#elif MULTI_READ_CASE==2
    auto first=read(index);uint32_t pair0=first*3+value,pair1=(first+1)*3+value;
    uint32_t second=value&1?read(32+(pair0&15)):0;
    uint32_t third=read(64+(second&7));
    job.result=(uint64_t(pair1+third)<<32)|(second+first);
#elif MULTI_READ_CASE==3
    job.result=uint64_t(index)*value+base;
#elif MULTI_READ_CASE==4
    read(index);job.result=value+7;
#elif MULTI_READ_CASE==6
    uint32_t a=read(index),b=read(index+1);
    volatile float product=bitsFloat(a)*bitsFloat(b);
    job.result=(uint64_t(a)<<32)|floatBits(0.5f+product);
#else
    auto a=read(index),b=read(index);job.result=(uint64_t(a)<<32)|b;
#endif
    return job;
}
int main() {
    VSourceMultiReadTop d;
    unsigned clocks=0,totalRequests=0,totalResults=0,maxPending=0;
    d.clk=0;d.reset=1;d.command_valid_in=0;d.response_ready_in=0;
    d.operation_in=0;d.index_in=0;d.value_in=0;
    d.memory_out___05Fready_in=0;d.memory_out___05Fvalid_in=0;
    d.memory_out___05Fdata_in=0;d.memory_out___05Ferror_in=0;
    auto edge=[&]{d.clk=1;d.eval();d.clk=0;d.eval();++clocks;};
    auto reset=[&]{d.reset=1;d.command_valid_in=0;d.memory_out___05Fvalid_in=0;d.eval();edge();d.reset=0;d.eval();};
    try {
        reset();
        for(unsigned campaign=0;campaign<3;++campaign) {
            bool cancel=campaign==0,errors=campaign==2;
            std::vector<Job> jobs;for(unsigned id=0;id<128;++id)jobs.push_back(reference(id,errors));
            struct Reply {unsigned due;Access access;};std::deque<Reply> pending;
            unsigned sent=0,received=0,requests=0,responses=0;
            bool heldResult=false,heldRequest=false;
            uint64_t savedResult=0;uint32_t savedFault=0,savedAddress=0,savedSize=0;
            for(unsigned tick=0;tick<20000&&received<128;++tick) {
                d.command_valid_in=sent<128;d.operation_in=0x10000+sent*0x1000;
                d.index_in=sent%8;d.value_in=sent;
                // Long full-pipeline stalls expose cyclic reply dependencies.
                d.response_ready_in=!cancel&&tick%257>=110&&tick%19>=7;
                d.memory_out___05Fready_in=tick%17>=8;
                bool replyValid=!pending.empty()&&pending.front().due<=clocks;
                d.memory_out___05Fvalid_in=replyValid;
                d.memory_out___05Fdata_in=replyValid?pending.front().access.data:0;
                d.memory_out___05Ferror_in=replyValid&&pending.front().access.error;
                d.eval();
                if(heldResult&&(!d.response_valid_out||d.result_out!=savedResult||d.fault_out!=savedFault))
                    throw std::runtime_error("result changed under backpressure");
                if(heldRequest&&(!d.memory_out___05Fvalid_out||d.memory_out___05Faddr_out!=savedAddress||d.memory_out___05Fsize_out!=savedSize))
                    throw std::runtime_error("arbiter changed a stalled request");
                bool take=d.command_valid_in&&d.command_ready_out;
                bool give=d.response_valid_out&&d.response_ready_in;
                bool request=d.memory_out___05Fvalid_out&&d.memory_out___05Fready_in;
                bool reply=replyValid&&d.memory_out___05Fready_out;
                if(take)++sent;
                Access access{};
                if(request) {
                    uint32_t address=d.memory_out___05Faddr_out;
                    unsigned id=address>=0x10000?(address-0x10000)/0x1000:128;
                    if(id>=sent||id>=128||jobs[id].reads.empty()||jobs[id].reads.front().address!=address||
                        d.memory_out___05Fwrite_out||d.memory_out___05Fsize_out!=4) {
                        std::fprintf(stderr,"unexpected request address=%x id=%u sent=%u expected=%x\n",address,id,sent,
                            id<128&&!jobs[id].reads.empty()?jobs[id].reads.front().address:0);
                        throw std::runtime_error("extra, missing, reordered or incorrectly predicated read");
                    }
                    access=jobs[id].reads.front();jobs[id].reads.pop_front();++requests;
                }
                if(give) {
                    if(received>=sent||!jobs[received].reads.empty()||d.fault_out!=jobs[received].fault||
                        (!d.fault_out&&d.result_out!=jobs[received].result)) {
                        std::fprintf(stderr,"result id=%u actual=%llx/%u expected=%llx/%u remaining=%zu\n",received,
                            (unsigned long long)d.result_out,d.fault_out,(unsigned long long)jobs[received].result,
                            jobs[received].fault,jobs[received].reads.size());
                        throw std::runtime_error("context, value, fault or output order mismatch");
                    }
                    ++received;
                }
                heldResult=d.response_valid_out&&!d.response_ready_in;savedResult=d.result_out;savedFault=d.fault_out;
                heldRequest=d.memory_out___05Fvalid_out&&!d.memory_out___05Fready_in;
                savedAddress=d.memory_out___05Faddr_out;savedSize=d.memory_out___05Fsize_out;
                edge();
                if(reply){pending.pop_front();++responses;}
                if(request)pending.push_back({clocks+5+requests%11,access});
                maxPending=std::max(maxPending,unsigned(pending.size()));
                if(cancel&&tick==80)break;
            }
            if(cancel){reset();continue;}
            if(sent!=128||received!=128||requests!=responses||!pending.empty())
                throw std::runtime_error("memory pipeline deadlocked or failed to drain");
            totalRequests+=requests;totalResults+=received;
            d.command_valid_in=0;d.memory_out___05Fvalid_in=0;d.response_ready_in=1;d.eval();
            for(unsigned i=0;i<32;++i){if(d.response_valid_out)throw std::runtime_error("duplicate result");edge();}
            reset();
        }
        if(MULTI_READ_CASE!=3&&(maxPending<2||maxPending>SOURCE_READ_DEPTH))throw std::runtime_error("request window serialized or overflowed");
        std::printf("PASS multiple source reads case=%u results=%u requests=%u outstanding=%u clocks=%u stalls/errors/reset\n",
                    MULTI_READ_CASE,totalResults,totalRequests,maxPending,clocks);
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s clocks=%u\n",e.what(),clocks);return 1;}
}
