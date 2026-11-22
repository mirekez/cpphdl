#include "VReadSafetyTop.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <string>

#ifndef READ_PATTERN
#error READ_PATTERN required
#endif

struct Agent {
    struct Reply {uint64_t data;unsigned due;};
    std::array<uint64_t,32> words{};
    std::deque<Reply> pending;
    unsigned logical=0,reads=0,returned=0,stalls=0,forced_stalls=0;
    unsigned returned_at_third=0;
    bool overlap12=false,overlap34=false;
    void resetCase() {
        words.fill(0);words[0]=3;words[1]=2;words[2]=101;
        words[3]=203;words[4]=307;words[5]=409;
        pending.clear();reads=returned=stalls=forced_stalls=returned_at_third=0;
        overlap12=overlap34=false;
    }
    void accept(uint32_t addr,unsigned size,uint32_t first_addr) {
        const uint32_t expected[] = {first_addr,0x1010,
            READ_PATTERN==4 ? 0x1018u : 0u,READ_PATTERN==4 ? 0x1020u : 0u};
        const unsigned max_reads=READ_PATTERN==4 ? 4u:2u;
        const uint32_t second=READ_PATTERN==2 ? 0x1018u:0x1010u;
        if(reads>=max_reads || addr!=(reads==1 ? second:expected[reads]) || size!=8)
            throw std::runtime_error("safety read address/order mismatch");
        if(reads==1)overlap12=returned==0;
        if(reads==2)returned_at_third=returned;
        if(reads==3)overlap34=returned==returned_at_third;
        pending.push_back({words[(addr-0x1000)/8],logical+9});++reads;
    }
};

int main() {
    VerilatedContext context;context.threads(1);
    VReadSafetyTop dut(&context);
    Agent agent;agent.resetCase();
    dut.clk=0;dut.reset=1;dut.command_valid_in=0;dut.response_ready_in=0;
    dut.operation_in=0x1000;dut.index_in=0x1010;dut.value_in=0;
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
        if(request && dut.memory_out___05Fwrite_out)
            throw std::runtime_error("read-only safety method wrote memory");
        dut.clk=1;dut.eval();dut.clk=0;dut.eval();
        if(response) {
            if(agent.pending.empty())throw std::runtime_error("unsolicited safety reply");
            agent.pending.pop_front();++agent.returned;
        }
        if(request)agent.accept(address,size,dut.operation_in);
        if(++clocks>100000)throw std::runtime_error("safety run timed out");
    };
    struct Snapshot {bool ready,valid;uint64_t result;uint32_t fault;};
    auto snapshot=[&] {return Snapshot{bool(dut.command_ready_out),bool(dut.response_valid_out),
                                     dut.result_out,dut.fault_out};};
    auto transition=[&](bool valid,bool ready,uint32_t first_addr=0x1000) {
        dut.reset=0;dut.command_valid_in=valid;dut.response_ready_in=ready;
        dut.operation_in=first_addr;dut.index_in=0x1010;dut.value_in=0;
        dut.memory_out___05Fready_in=1;dut.eval();
        if(dut.memory_out___05Fvalid_out && agent.forced_stalls<3) {
            dut.memory_out___05Fready_in=0;++agent.forced_stalls;
        }
        const bool due=!agent.pending.empty() && agent.pending.front().due<=agent.logical;
        dut.memory_out___05Fvalid_in=due;
        dut.memory_out___05Fdata_in=due ? agent.pending.front().data:0;
        dut.memory_out___05Ferror_in=0;dut.eval();
        if(dut.memory_out___05Fvalid_out && !dut.memory_out___05Fready_in)++agent.stalls;
#ifdef READ_RETIMED
        if(!dut.retiming_ready_out)throw std::runtime_error("safety transition not admitted");
        // A zero-latency mapping can already commit at admission. Wait
        // without consuming it, then sample and commit exactly one edge.
        while(!dut.retiming_commit_out)edge();
        auto result=snapshot();edge();
#else
        auto result=snapshot();edge();
#endif
        ++agent.logical;return result;
    };
    auto boot=[&] {
        dut.reset=1;dut.command_valid_in=0;dut.memory_out___05Fvalid_in=0;
        agent.pending.clear();edge();dut.reset=0;
        for(unsigned i=0;i<100;++i)if(transition(false,false).ready)return;
        throw std::runtime_error("safety controller failed to boot");
    };
    auto run=[&](uint32_t first_addr,uint64_t expected,unsigned expected_reads) {
        transition(true,false,first_addr);
        Snapshot result{};
        for(unsigned i=0;i<500;++i) {
            result=transition(false,false,first_addr);
            if(result.valid)break;
        }
        if(!result.valid || result.fault || result.result!=expected ||
           agent.reads!=expected_reads || agent.returned!=expected_reads ||
           !agent.pending.empty())
            throw std::runtime_error("safety result/traffic mismatch");
        if(!transition(false,true,first_addr).valid)
            throw std::runtime_error("safety response consume mismatch");
    };
    try {
        boot();run(0x1000,READ_PATTERN==2 ? 4318u:READ_PATTERN==4 ? 614u:104u,
                   READ_PATTERN==4 ? 4u:2u);
        if(agent.overlap12!=(READ_PATTERN==2 || READ_PATTERN==4) ||
           (READ_PATTERN==4 && !agent.overlap34) || agent.stalls<3)
            throw std::runtime_error("unsafe read grouping or missing pair overlap: overlap12="+
                std::to_string(agent.overlap12)+" overlap34="+
                std::to_string(agent.overlap34)+" stalls="+
                std::to_string(agent.stalls));
        if(READ_PATTERN==3) {
            boot();agent.resetCase();
            run(0x1008,2,1);
            if(agent.overlap12)throw std::runtime_error("branch-false read was speculated");
        }
        std::printf("PASS safety pattern=%u raw/retimed=%u reads=%u overlap12=%u overlap34=%u clocks=%u\n",
            READ_PATTERN,
#ifdef READ_RETIMED
            1u,
#else
            0u,
#endif
            agent.reads,unsigned(agent.overlap12),unsigned(agent.overlap34),clocks);
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL safety pattern=%u clock=%u logical=%u reads=%u returned=%u: %s\n",
            READ_PATTERN,clocks,agent.logical,agent.reads,agent.returned,error.what());return 1;
    }
}
