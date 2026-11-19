#include "VConcurrentWideTop.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>

int main() {
    VerilatedContext context;context.threads(1);
    VConcurrentWideTop dut(&context);
    std::array<uint32_t,32> words{};
    for(unsigned i=0;i<32;++i)words[i]=0x12000000u+i*0x01020304u;
    struct Reply {unsigned due;std::array<uint32_t,16> data;};
    std::deque<Reply> pending;
    unsigned clocks=0,logical=0,reads=0,replies=0,stalls=0;
    bool overlap=false;
    dut.clk=0;dut.reset=1;dut.command_valid_in=0;dut.response_ready_in=0;
    dut.operation_in=0x1000;dut.index_in=0x1040;dut.value_in=0;
    dut.memory_out___05Fready_in=0;dut.memory_out___05Fvalid_in=0;
    dut.memory_out___05Ferror_in=0;
    for(unsigned i=0;i<16;++i)dut.memory_out___05Fdata_in[i]=0;
    auto edge=[&] {
        dut.clk=0;dut.eval();
#ifdef WIDE_READ_RETIMED
        const bool commit=dut.retiming_commit_out && !dut.reset;
#else
        const bool commit=!dut.reset;
#endif
        const bool request=commit && dut.memory_out___05Fvalid_out && dut.memory_out___05Fready_in;
        const bool response=commit && dut.memory_out___05Fvalid_in && dut.memory_out___05Fready_out;
        const uint32_t addr=dut.memory_out___05Faddr_out;
        const unsigned size=dut.memory_out___05Fsize_out;
        if(request && dut.memory_out___05Fwrite_out)
            throw std::runtime_error("unexpected write");
        dut.clk=1;dut.eval();dut.clk=0;dut.eval();
        if(response) {
            if(pending.empty())throw std::runtime_error("unexpected reply");
            pending.pop_front();++replies;
        }
        if(request) {
            if(reads>=2 || addr!=(reads ? 0x1040u:0x1000u) || size!=64)
                throw std::runtime_error("wide paired address or size mismatch");
            if(reads==1)overlap=replies==0;
            Reply reply{};reply.due=logical+9;
            for(unsigned i=0;i<16;++i)reply.data[i]=words[(addr-0x1000)/4+i];
            pending.push_back(reply);++reads;
        }
        if(++clocks>100000)throw std::runtime_error("wide paired timeout");
    };
    struct Snapshot {bool ready,valid;uint64_t result;uint32_t fault;};
    auto step=[&](bool valid,bool ready) {
        dut.reset=0;dut.command_valid_in=valid;dut.response_ready_in=ready;
        dut.memory_out___05Fready_in=(logical%5)!=0;
        const bool due=!pending.empty() && pending.front().due<=logical;
        dut.memory_out___05Fvalid_in=due;
        dut.memory_out___05Ferror_in=0;
        for(unsigned i=0;i<16;++i)
            dut.memory_out___05Fdata_in[i]=due ? pending.front().data[i]:0;
        dut.eval();
        if(dut.memory_out___05Fvalid_out && !dut.memory_out___05Fready_in)++stalls;
#ifdef WIDE_READ_RETIMED
        if(!dut.retiming_ready_out)throw std::runtime_error("transition not admitted");
        if(!dut.retiming_commit_out) {
            edge();while(!dut.retiming_commit_out)edge();
        }
        Snapshot result{bool(dut.command_ready_out),bool(dut.response_valid_out),
                        dut.result_out,dut.fault_out};edge();
#else
        Snapshot result{bool(dut.command_ready_out),bool(dut.response_valid_out),
                        dut.result_out,dut.fault_out};edge();
#endif
        ++logical;return result;
    };
    try {
        edge();dut.reset=0;
        bool ready=false;
        for(unsigned i=0;i<100;++i)if(step(false,false).ready){ready=true;break;}
        if(!ready)throw std::runtime_error("controller did not boot");
        step(true,false);
        Snapshot result{};
        for(unsigned i=0;i<300;++i)if((result=step(false,false)).valid)break;
        if(!result.valid || result.fault ||
           result.result!=uint64_t(words[15])+words[30] ||
           reads!=2 || replies!=2 || !overlap || !pending.empty()) {
            std::fprintf(stderr,"wide state valid=%u fault=%u result=%016llx expected=%016llx overlap=%u pending=%zu\n",
                unsigned(result.valid),result.fault,
                static_cast<unsigned long long>(result.result),
                static_cast<unsigned long long>(uint64_t(words[15])+words[30]),
                unsigned(overlap),pending.size());
            throw std::runtime_error("wide paired result/overlap mismatch");
        }
        for(unsigned i=0;i<6;++i) {
            auto held=step(false,false);
            if(!held.valid || held.result!=result.result || held.fault || reads!=2)
                throw std::runtime_error("wide paired output did not hold");
        }
        const auto consumed=step(false,true),after=step(false,false);
        if(!consumed.valid || after.valid) {
            std::fprintf(stderr,"consume sampled_valid=%u after_valid=%u after_ready=%u\n",
                unsigned(consumed.valid),unsigned(after.valid),unsigned(after.ready));
            throw std::runtime_error("wide paired consume mismatch");
        }
        std::printf("PASS concurrent wide reads overlap=%u reads=%u replies=%u stalls=%u clocks=%u logical=%u\n",
                    unsigned(overlap),reads,replies,stalls,clocks,logical);
    }catch(const std::exception& error){
        std::fprintf(stderr,"FAIL wide paired clock=%u logical=%u reads=%u replies=%u: %s\n",
                     clocks,logical,reads,replies,error.what());return 1;
    }
}
