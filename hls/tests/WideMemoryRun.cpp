#include "VWideMemoryTop.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>

struct WideAgent {
    struct Reply { std::array<uint32_t,16> data{}; unsigned due; bool error; };
    std::array<uint32_t,64> words{};
    std::deque<Reply> pending;
    unsigned reads=0,writes=0,replies=0,backpressure=0,logical=0,delay=3;
    bool error_next_read=false,error_next_write=false;
    void initialize() {
        words.fill(0);
        for(unsigned i=0;i<16;++i)words[i]=0x11000000u+i*0x01010101u;
        words[16]=0x13579bdfu;
    }
    void accept(uint32_t address,unsigned size,bool write,const std::array<uint32_t,16>& data) {
        if(address<0x1000 || address+size>0x1100 ||
           (size!=4 && size!=64) || address%size) {
            std::fprintf(stderr,"invalid wide request addr=%08x size=%u write=%u logical=%u\n",
                         address,size,unsigned(write),logical);
            throw std::runtime_error("invalid accepted wide memory transaction");
        }
        const unsigned offset=(address-0x1000)/4;
        Reply reply{};reply.due=logical+delay+(reads%3);
        if(write) {
            if(address!=0x1080 || size!=64)throw std::runtime_error("unexpected wide write");
            for(unsigned i=0;i<16;++i)words[offset+i]=data[i];
            reply.error=error_next_write;
            error_next_write=false;
            ++writes;
        } else {
            for(unsigned i=0;i<size/4;++i)reply.data[i]=words[offset+i];
            reply.error=error_next_read;
            error_next_read=false;
            ++reads;
        }
        pending.push_back(reply);
    }
    uint64_t expected() const {
        const uint32_t scalar=words[16],member=words[3];
        const uint32_t high=(0x11000000u+15*0x01010101u)^(scalar+member);
        const uint32_t low=(0x11000000u+7*0x01010101u)+member;
        return (uint64_t(high)<<32)|low;
    }
    void verify() const {
        for(unsigned lane=0;lane<16;++lane) {
            uint32_t expected=0x11000000u+lane*0x01010101u;
            if(lane==0)expected^=words[16];
            if(lane==7)expected+=words[3];
            if(lane==15)expected^=words[16]+words[3];
            if(words[32+lane]!=expected) {
                std::fprintf(stderr,"lane %u got %08x expected %08x\n",
                    lane,words[32+lane],expected);
                throw std::runtime_error("wide output mismatch");
            }
        }
    }
};

int main() {
    VerilatedContext context;context.threads(1);
    VWideMemoryTop dut(&context);
    WideAgent agent;agent.initialize();
    dut.clk=0;dut.reset=1;dut.command_valid_in=0;dut.response_ready_in=0;
    dut.operation_in=0x1000;dut.index_in=0x1040;dut.value_in=0x1080;
    dut.memory_out___05Fready_in=0;dut.memory_out___05Fvalid_in=0;
    dut.memory_out___05Ferror_in=0;
    for(unsigned i=0;i<16;++i)dut.memory_out___05Fdata_in[i]=0;
    unsigned clocks=0;
    auto edge=[&] {
        dut.clk=0;dut.eval();
#ifdef WIDE_RETIMED
        const bool commit=dut.retiming_commit_out && !dut.reset;
#else
        const bool commit=!dut.reset;
#endif
        const bool request=commit && dut.memory_out___05Fvalid_out && dut.memory_out___05Fready_in;
        const bool response=commit && dut.memory_out___05Fvalid_in && dut.memory_out___05Fready_out;
        std::array<uint32_t,16> data{};
        if(request)for(unsigned i=0;i<16;++i)data[i]=dut.memory_out___05Fdata_out[i];
        const uint32_t address=dut.memory_out___05Faddr_out;
        const unsigned size=dut.memory_out___05Fsize_out;
        const bool write=dut.memory_out___05Fwrite_out;
        dut.clk=1;dut.eval();dut.clk=0;dut.eval();
        if(response) {
            if(agent.pending.empty())throw std::runtime_error("unexpected memory reply");
            agent.pending.pop_front();++agent.replies;
        }
        if(request)agent.accept(address,size,write,data);
        if(++clocks>200000)throw std::runtime_error("wide memory timeout");
    };
    struct Snapshot {bool ready,valid;uint64_t result;uint32_t fault;};
    auto snapshot=[&] {return Snapshot{bool(dut.command_ready_out),bool(dut.response_valid_out),
                                     dut.result_out,dut.fault_out};};
    auto transition=[&](bool valid,bool ready,uint32_t source=0x1000,
                        uint32_t scalar=0x1040,uint32_t destination=0x1080) {
        dut.reset=0;dut.command_valid_in=valid;dut.response_ready_in=ready;
        dut.operation_in=source;dut.index_in=scalar;dut.value_in=destination;
        dut.memory_out___05Fready_in=(agent.logical%5)!=0;
        const bool due=!agent.pending.empty() && agent.pending.front().due<=agent.logical;
        dut.memory_out___05Fvalid_in=due && (agent.logical%7)!=0;
        dut.memory_out___05Ferror_in=due && agent.pending.front().error;
        for(unsigned i=0;i<16;++i)
            dut.memory_out___05Fdata_in[i]=due ? agent.pending.front().data[i] : 0;
        dut.eval();
        if(dut.memory_out___05Fvalid_out && !dut.memory_out___05Fready_in)
            ++agent.backpressure;
#ifdef WIDE_RETIMED
        if(!dut.retiming_ready_out)throw std::runtime_error("wide transition not admitted");
        edge();
        while(!dut.retiming_commit_out)edge();
        auto result=snapshot();
        edge();
#else
        auto result=snapshot();
        edge();
#endif
        ++agent.logical;
        return result;
    };
    auto reset=[&] {
        dut.reset=1;dut.command_valid_in=0;dut.memory_out___05Fvalid_in=0;
        agent.pending.clear();edge();dut.reset=0;
        for(unsigned i=0;i<200;++i)if(transition(false,false).ready)return;
        throw std::runtime_error("wide controller boot did not become ready");
    };
    auto run=[&](uint32_t source,uint32_t destination,uint32_t expected_fault) {
        if(!transition(true,false,source,0x1040,destination).ready)
            throw std::runtime_error("wide command not accepted");
        Snapshot result{};
        for(unsigned i=0;i<2000;++i) {
            result=transition(false,false);
            if(result.valid)break;
        }
        if(!result.valid || result.fault!=expected_fault ||
           (!expected_fault && result.result!=agent.expected())) {
            std::fprintf(stderr,"result valid=%u fault=%u data=%016llx expectedfault=%u\n",
                unsigned(result.valid),result.fault,
                static_cast<unsigned long long>(result.result),expected_fault);
            throw std::runtime_error("wide command result mismatch");
        }
        const unsigned old_reads=agent.reads,old_writes=agent.writes;
        for(unsigned i=0;i<6;++i) {
            auto held=transition(false,false);
            if(!held.valid || held.result!=result.result || held.fault!=result.fault ||
               agent.reads!=old_reads || agent.writes!=old_writes)
                throw std::runtime_error("wide held output changed or repeated traffic");
        }
        if(!transition(false,true).valid || transition(false,false).valid)
            throw std::runtime_error("wide result hold/consume mismatch");
    };
    try {
        reset();
        run(0x1000,0x1080,0);agent.verify();
        if(agent.reads!=3 || agent.writes!=1 || agent.replies!=4)
            throw std::runtime_error("wide normal transaction counts");
        for(uint32_t misaligned : {0x1008u,0x1010u,0x1020u}) {
            reset();auto old=agent.reads;
            run(misaligned,0x1080,3);
            if(agent.reads!=old)throw std::runtime_error("misaligned wide read reached bus");
        }
        reset();auto old_writes=agent.writes;
        run(0x1000,0x1084,3);
        if(agent.writes!=old_writes)throw std::runtime_error("misaligned wide write reached bus");
        reset();agent.error_next_read=true;old_writes=agent.writes;
        run(0x1000,0x1080,5);
        if(agent.writes!=old_writes)throw std::runtime_error("write occurred after read error");
        reset();agent.error_next_write=true;old_writes=agent.writes;
        run(0x1000,0x1080,5);
        if(agent.writes!=old_writes+1)
            throw std::runtime_error("write acknowledgement fault not exercised");
        reset();agent.delay=80;
        transition(true,false);
        auto old_reads=agent.reads;
        for(unsigned i=0;i<100 && agent.reads==old_reads;++i)transition(false,false);
        if(agent.reads==old_reads || agent.pending.empty())
            throw std::runtime_error("no pending read to cancel");
        reset();agent.delay=3;
        for(unsigned i=0;i<8;++i)if(transition(false,false).valid)
            throw std::runtime_error("stale result after reset");
        for(unsigned i=32;i<48;++i)agent.words[i]=0;
        run(0x1000,0x1080,0);agent.verify();
        std::printf("PASS wide %s clocks=%u logical=%u reads=%u writes=%u replies=%u backpressure=%u\n",
#ifdef WIDE_RETIMED
            "retimed",
#else
            "raw",
#endif
            clocks,agent.logical,agent.reads,agent.writes,agent.replies,agent.backpressure);
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL wide %s clock=%u logical=%u reads=%u writes=%u replies=%u: %s\n",
#ifdef WIDE_RETIMED
            "retimed",
#else
            "raw",
#endif
            clocks,agent.logical,agent.reads,agent.writes,agent.replies,error.what());
        return 1;
    }
}
