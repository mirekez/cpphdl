#include "VConcurrentReadTop.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>

#ifndef READ_WINDOW
#define READ_WINDOW 2
#endif
#ifndef DEPENDENT_SECOND_READ
#define DEPENDENT_SECOND_READ 0
#endif

struct ReadAgent {
    struct Reply {uint64_t data;unsigned due;bool error;bool write;};
    std::array<uint64_t,32> words{};
    std::deque<Reply> pending;
    unsigned logical=0,reads=0,writes=0,read_replies=0,replies=0,stalls=0,forced_stalls=0;
    unsigned delay=9,error_read_ordinal=0;
    bool error_write=false,overlap=false;
    bool hold_second_until_first_error=false,second_held=false,first_error_while_held=false;
    bool held_request=false,held_write=false;
    uint32_t held_address=0;unsigned held_size=0;uint64_t held_data=0;
    void initialize() {
        words.fill(0);words[0]=3;words[2]=101;words[3]=203;
        words[4]=307;words[5]=409;
        pending.clear();reads=writes=read_replies=replies=stalls=forced_stalls=0;
        overlap=false;error_read_ordinal=0;error_write=false;held_request=false;
        hold_second_until_first_error=false;second_held=false;first_error_while_held=false;
    }
    void accept(uint32_t address,unsigned size,bool write,uint64_t data) {
        if(address<0x1000 || address+size>0x1100 || size!=8 || (address&7))
            throw std::runtime_error("bad accepted 64-bit memory request");
        if(write) {
            if(address!=0x1080 || reads!=2 || read_replies!=2)
                throw std::runtime_error("write issued before reads retired");
            words[(address-0x1000)/8]=data;++writes;
            pending.push_back({0,logical+3,error_write,true});
            error_write=false;
        } else {
            const uint32_t expected_address=reads==0 ? 0x1000 :
                (DEPENDENT_SECOND_READ ? 0x1028 : 0x1010);
            if(reads>=2 || address!=expected_address)
                throw std::runtime_error("read order/address mismatch");
            if(reads==1) {
                overlap=read_replies==0;
                if((DEPENDENT_SECOND_READ || READ_WINDOW==1) && overlap)
                    throw std::runtime_error("dependent/default read overlapped");
            }
            ++reads;
            pending.push_back({words[(address-0x1000)/8],logical+delay,
                               reads==error_read_ordinal,false});
        }
    }
};

int main() {
    VerilatedContext context;context.threads(1);
    VConcurrentReadTop dut(&context);
    ReadAgent agent;agent.initialize();
    dut.clk=0;dut.reset=1;dut.command_valid_in=0;dut.response_ready_in=0;
    dut.operation_in=0x1000;dut.index_in=0x1010;dut.value_in=0x1080;
    dut.memory_out___05Fready_in=0;dut.memory_out___05Fvalid_in=0;
    dut.memory_out___05Fdata_in=0;dut.memory_out___05Ferror_in=0;
    unsigned clocks=0;
    auto edge=[&] {
        dut.clk=0;dut.eval();
#ifdef READ_RETIMED
        const bool commit=dut.retiming_commit_out && !dut.reset;
#else
        const bool commit=!dut.reset;
#endif
        const bool request=commit && dut.memory_out___05Fvalid_out && dut.memory_out___05Fready_in;
        const bool response=commit && dut.memory_out___05Fvalid_in && dut.memory_out___05Fready_out;
        const uint32_t address=dut.memory_out___05Faddr_out;
        const unsigned size=dut.memory_out___05Fsize_out;
        const bool write=dut.memory_out___05Fwrite_out;
        const uint64_t data=dut.memory_out___05Fdata_out;
        dut.clk=1;dut.eval();dut.clk=0;dut.eval();
        if(response) {
            if(agent.pending.empty())throw std::runtime_error("unexpected response");
            if(agent.hold_second_until_first_error && agent.second_held &&
               !agent.pending.front().write && agent.pending.front().error &&
               agent.read_replies==0)agent.first_error_while_held=true;
            if(!agent.pending.front().write)++agent.read_replies;
            agent.pending.pop_front();++agent.replies;
        }
        if(request)agent.accept(address,size,write,data);
        if(++clocks>200000)throw std::runtime_error("concurrent-read timeout");
    };
    struct Snapshot {bool ready,valid;uint64_t result;uint32_t fault;};
    auto snapshot=[&] {return Snapshot{bool(dut.command_ready_out),bool(dut.response_valid_out),
                                     dut.result_out,dut.fault_out};};
    auto transition=[&](bool valid,bool ready,uint32_t a=0x1000,
                        uint32_t b=0x1010,uint32_t c=0x1080) {
        dut.reset=0;dut.command_valid_in=valid;dut.response_ready_in=ready;
        dut.operation_in=a;dut.index_in=b;dut.value_in=c;
        dut.memory_out___05Fready_in=1;
        dut.eval();
        if(agent.hold_second_until_first_error && agent.reads==1 &&
           agent.read_replies==0 && dut.memory_out___05Fvalid_out) {
            dut.memory_out___05Fready_in=0;agent.second_held=true;
        } else if(dut.memory_out___05Fvalid_out && agent.forced_stalls<3) {
            dut.memory_out___05Fready_in=0;
            ++agent.forced_stalls;
        } else dut.memory_out___05Fready_in=(agent.logical%7)!=0;
        const bool due=!agent.pending.empty() && agent.pending.front().due<=agent.logical;
        dut.memory_out___05Fvalid_in=due && (agent.logical%5)!=0;
        dut.memory_out___05Fdata_in=due ? agent.pending.front().data : 0;
        dut.memory_out___05Ferror_in=due && agent.pending.front().error;
        dut.eval();
        if(agent.held_request && (!dut.memory_out___05Fvalid_out ||
           dut.memory_out___05Faddr_out!=agent.held_address ||
           dut.memory_out___05Fsize_out!=agent.held_size ||
           bool(dut.memory_out___05Fwrite_out)!=agent.held_write ||
           dut.memory_out___05Fdata_out!=agent.held_data))
            throw std::runtime_error("held request payload changed under backpressure");
        agent.held_request=dut.memory_out___05Fvalid_out && !dut.memory_out___05Fready_in;
        if(agent.held_request) {
            agent.held_address=dut.memory_out___05Faddr_out;
            agent.held_size=dut.memory_out___05Fsize_out;
            agent.held_write=dut.memory_out___05Fwrite_out;
            agent.held_data=dut.memory_out___05Fdata_out;
        }
        if(dut.memory_out___05Fvalid_out && !dut.memory_out___05Fready_in)++agent.stalls;
#ifdef READ_RETIMED
        if(!dut.retiming_ready_out)throw std::runtime_error("retimed transition not admitted");
        // A zero-latency mapping can already commit at admission. Wait
        // without consuming it, then sample and commit exactly one edge.
        while(!dut.retiming_commit_out)edge();
        auto result=snapshot();edge();
#else
        auto result=snapshot();edge();
#endif
        ++agent.logical;return result;
    };
    auto reset=[&] {
        dut.reset=1;dut.command_valid_in=0;dut.memory_out___05Fvalid_in=0;
        agent.pending.clear();agent.held_request=false;edge();dut.reset=0;
        for(unsigned i=0;i<200;++i)if(transition(false,false).ready)return;
        throw std::runtime_error("read controller failed to boot");
    };
    unsigned first_latency_logical=0,first_latency_clocks=0;
    auto run=[&](uint32_t expected_fault=0,uint32_t a=0x1000,
                 uint32_t b=0x1010,uint32_t c=0x1080) {
        const unsigned start_logical=agent.logical,start_clocks=clocks;
        transition(true,false,a,b,c);
        Snapshot result{};
        for(unsigned i=0;i<2000;++i) {
            result=transition(false,false);
            if(result.valid)break;
        }
        const uint64_t expected=DEPENDENT_SECOND_READ ? 412u : 104u;
        if(!result.valid || result.fault!=expected_fault ||
           (!expected_fault && result.result!=expected) || !agent.pending.empty()) {
            std::fprintf(stderr,"valid=%u fault=%u result=%llu expectedfault=%u pending=%zu\n",
                unsigned(result.valid),result.fault,
                static_cast<unsigned long long>(result.result),
                expected_fault,agent.pending.size());
            throw std::runtime_error("concurrent-read result/fault mismatch");
        }
        if(!first_latency_logical) {
            first_latency_logical=agent.logical-start_logical;
            first_latency_clocks=clocks-start_clocks;
        }
        const auto read_count=agent.reads,write_count=agent.writes;
        for(unsigned i=0;i<6;++i) {
            auto held=transition(false,false);
            if(!held.valid || held.fault!=result.fault || held.result!=result.result ||
               agent.reads!=read_count || agent.writes!=write_count)
                throw std::runtime_error("held response changed or repeated traffic");
        }
        if(!transition(false,true).valid || transition(false,false).valid)
            throw std::runtime_error("response consume mismatch");
    };
    try {
        reset();run();
        if(agent.reads!=2 || agent.writes!=1 || agent.replies!=3 ||
           agent.words[16]!=(DEPENDENT_SECOND_READ ? 412u : 104u) ||
           agent.overlap!=(READ_WINDOW==2 && !DEPENDENT_SECOND_READ))
            throw std::runtime_error("normal read overlap/traffic result mismatch");
        reset();agent.initialize();agent.error_read_ordinal=1;
        run(5);
        if(agent.writes)throw std::runtime_error("write after first read error");
        if(READ_WINDOW==2 && !DEPENDENT_SECOND_READ) {
            reset();agent.initialize();agent.error_read_ordinal=1;
            agent.hold_second_until_first_error=true;
            run(5);
            if(!agent.first_error_while_held || agent.reads!=2 ||
               agent.read_replies!=2 || agent.writes)
                throw std::runtime_error("held second request did not drain after first error");
        }
        reset();agent.initialize();agent.error_read_ordinal=2;
        run(5);
        if(agent.writes)throw std::runtime_error("write after second read error");
        reset();agent.initialize();run(3,0x1004);
        if(agent.reads || agent.writes)
            throw std::runtime_error("first bad alignment reached bus");
        reset();agent.initialize();run(3,0x1000,0x1014);
        if(agent.reads!=1 || agent.writes)
            throw std::runtime_error("second bad alignment did not retire first read");
        reset();agent.initialize();agent.error_read_ordinal=1;
        run(5,0x1000,0x1014);
        if(agent.reads!=1 || agent.writes)
            throw std::runtime_error("first bus error lost to second alignment error");
        reset();agent.initialize();run(3,0x1000,0x1010,0x1084);
        if(agent.writes)throw std::runtime_error("misaligned write reached bus");
        reset();agent.initialize();agent.error_write=true;run(5);
        if(agent.writes!=1)throw std::runtime_error("write-ack error not exercised");
        reset();agent.initialize();agent.delay=100;
        transition(true,false);
        for(unsigned i=0;i<100 && agent.reads<(READ_WINDOW==2 && !DEPENDENT_SECOND_READ ? 2u:1u);++i)
            transition(false,false);
        if(agent.reads<(READ_WINDOW==2 && !DEPENDENT_SECOND_READ ? 2u:1u) ||
           agent.read_replies || agent.pending.empty())
            throw std::runtime_error("no pending reads to cancel");
        reset();agent.initialize();agent.delay=9;
        for(unsigned i=0;i<8;++i)if(transition(false,false).valid)
            throw std::runtime_error("stale result after reset");
        run();
        if(agent.words[16]!=(DEPENDENT_SECOND_READ ? 412u:104u))
            throw std::runtime_error("reset recovery result mismatch");
        std::printf("PASS reads %s window=%u clocks=%u logical=%u overlap=%u stalls=%u first_latency_logical=%u first_latency_clocks=%u\n",
            DEPENDENT_SECOND_READ ? "dependent":"independent",READ_WINDOW,
            clocks,agent.logical,unsigned(agent.overlap),agent.stalls,
            first_latency_logical,first_latency_clocks);
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL reads %s window=%u clock=%u logical=%u reads=%u replies=%u: %s\n",
            DEPENDENT_SECOND_READ ? "dependent":"independent",READ_WINDOW,
            clocks,agent.logical,agent.reads,agent.replies,error.what());return 1;
    }
}
