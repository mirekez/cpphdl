#include "VSourceWriteTop.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <vector>
#ifndef WRITE_CASE
#define WRITE_CASE 1
#endif
using Memory=std::array<uint8_t,128>;
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
static Memory initial() {
    Memory memory{};for(unsigned i=0;i<32;++i) {
        uint32_t value=WRITE_CASE==5?0x3d000000u|((i*0x9e3779b9u)&0x01ffffffu):0x12560000u+i*13+7;
        store(memory,4*i,4,value);
    }return memory;
}
static Job reference(unsigned id,Memory& memory,std::deque<Access>& trace,bool errors) {
    Job job;job.value=WRITE_CASE==5?0x3d000000u|((id*0x9e3779b9u)&0x01ffffffu):0xfedc0000u+id*97u;
    unsigned index=id%4;uint32_t value=job.value;
    auto access=[&](bool write,unsigned offset,unsigned size,uint64_t data=0) {
        if(job.fault)return uint64_t(0);
        ++job.count;
        bool error=errors&&job.count==1+id%7;
        uint64_t reply=write?0:load(memory,offset,size);
        trace.push_back({id,job.count,0x1000+offset,size,write,error,data,reply});
        if(error)job.fault=5;
        else if(write)store(memory,offset,size,data);
        return error?uint64_t(0):reply;
    };
    auto read=[&](unsigned i){return uint32_t(access(false,4*i,4));};
    auto write=[&](unsigned i,uint32_t v){access(true,4*i,4,v);};
#if WRITE_CASE==0 || WRITE_CASE==6
    write(index,value);
#if WRITE_CASE==6
    write(index,value);
#endif
    job.result=value;
#elif WRITE_CASE==1
    uint32_t a=read(index);write(index,a+value);job.result=read(index);
#elif WRITE_CASE==2
    if(value&1)write(index,value^0xabc);
    job.result=read(index);
#elif WRITE_CASE==3
    write(index,value);write(index+1,value+1);
    uint32_t a=read(index),b=read(index+1);job.result=uint64_t(a)+b;
#elif WRITE_CASE==4
    access(true,4*index+2,2,uint16_t(value));job.result=read(index);
#elif WRITE_CASE==5
    uint32_t a=read(index),b=read(16+index);
    volatile float product=bitsFloat(b)*bitsFloat(value);
    write(index,floatBits(bitsFloat(a)+product));job.result=read(index);
#else
    if(value&1)write(index,value);
    job.result=value+3;
#endif
    return job;
}
int main() {
    VSourceWriteTop d;unsigned clocks=0,completed=0,totalReads=0,totalWrites=0,peakPending=0;
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
            for(unsigned id=0;id<128;++id)jobs.push_back(reference(id,expected,trace,errors));
            std::array<unsigned,128> acknowledged{};
            struct Pending {unsigned due;Access access;uint64_t data;};std::deque<Pending> pending;
            unsigned sent=0,received=0;
            bool heldRequest=false,heldResult=false;
            uint32_t heldAddress=0,heldSize=0,heldFault=0;bool heldWrite=false;
            uint64_t heldData=0,heldValue=0;
            for(unsigned tick=0;tick<120000&&received<128;++tick) {
                d.command_valid_in=sent<128;d.operation_in=0x1000;d.index_in=sent%4;d.value_in=jobs[sent<128?sent:0].value;
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
                    if(trace.empty())throw std::runtime_error("extra external effect");
                    access=trace.front();trace.pop_front();
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
                }
                if(d.response_valid_out) {
                    if(received>=sent||acknowledged[received]!=jobs[received].count)
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
                if(cancel&&tick==150)break;
            }
            if(!cancel&&(sent!=128||received!=128||!trace.empty()||!pending.empty()||memory!=expected))
                throw std::runtime_error("source effects failed to drain or final memory differs");
            reset();
        }
        std::printf("PASS source writes case=%u completed=%u reads=%u writes=%u outstanding=%u clocks=%u aliases/acknowledgements/faults/stalls/reset\n",
                    WRITE_CASE,completed,totalReads,totalWrites,peakPending,clocks);
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s clocks=%u\n",e.what(),clocks);return 1;}
}
