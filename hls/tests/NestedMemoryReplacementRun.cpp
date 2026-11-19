#include "VNestedMemoryTop.h"
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>

int main() {
    VNestedMemoryTop dut;
    dut.clk=0;dut.reset=1;dut.command_valid_in=0;dut.response_ready_in=0;
    dut.memory_out___05Fready_in=1;dut.memory_out___05Fvalid_in=0;
    dut.memory_out___05Fdata_in=0;dut.memory_out___05Ferror_in=0;
    dut.operation_in=0x1000;dut.index_in=1;dut.value_in=5;
    unsigned clocks=0,requests=0,responses=0,responseDelay=7;
    std::deque<unsigned> due;
    auto edge=[&] {
        dut.clk=0;
        dut.eval();
        const bool commit=dut.retiming_commit_out && !dut.reset;
        const bool request=commit && dut.memory_out___05Fvalid_out && dut.memory_out___05Fready_in;
        const bool response=commit && dut.memory_out___05Fvalid_in && dut.memory_out___05Fready_out;
        if(request && (dut.memory_out___05Fwrite_out ||
           dut.memory_out___05Faddr_out!=0x1004 || dut.memory_out___05Fsize_out!=4))
            throw std::runtime_error("wrong external read request");
        dut.clk=1;dut.eval();dut.clk=0;dut.eval();
        if(response){due.pop_front();++responses;}
        if(request){due.push_back(clocks+responseDelay);++requests;}
        if(++clocks>1200)throw std::runtime_error("nested memory atomic timed out");
    };
    struct Snapshot {bool ready,valid;uint64_t result;uint32_t fault;};
    auto transition=[&](bool valid,bool ready) {
        dut.reset=0;dut.command_valid_in=valid;dut.response_ready_in=ready;
        // A retimed FSM samples the whole external-input bundle at admission.
        // Keep the memory response stable until the matching commit edge.
        dut.memory_out___05Fvalid_in=!due.empty() && due.front()<=clocks;
        dut.memory_out___05Fdata_in=7;
        dut.operation_in=0x1000;dut.index_in=1;dut.value_in=5;dut.eval();
        if(!dut.retiming_ready_out)throw std::runtime_error("retimed command not admitted");
        edge();
        // Inputs may change after the retimer captures this logical step.
        dut.command_valid_in=!valid;dut.operation_in=99;dut.index_in=17;dut.value_in=123;
        dut.response_ready_in=!ready;dut.eval();
        while(!dut.retiming_commit_out)edge();
        Snapshot s{bool(dut.command_ready_out),bool(dut.response_valid_out),
                   dut.result_out,dut.fault_out};
        edge();
        return s;
    };
    try {
        edge();dut.reset=0;
        bool ready=false;
        for(unsigned i=0;i<10 && !ready;++i)ready=transition(false,false).ready;
        if(!ready)throw std::runtime_error("boot did not make command ready");
        if(!transition(true,false).ready)throw std::runtime_error("first command not accepted");
        constexpr uint64_t expected=(uint64_t(1)<<32)|45u;
        Snapshot result{};
        for(unsigned i=0;i<100;++i) {
            result=transition(false,false);
            if(result.valid)break;
        }
        if(!result.valid || result.result!=expected || result.fault || requests!=1 || responses!=1) {
            std::fprintf(stderr,"valid=%u result=%016llx fault=%u requests=%u responses=%u\n",
                unsigned(result.valid),static_cast<unsigned long long>(result.result),
                result.fault,requests,responses);
            throw std::runtime_error("nested atomic external read result mismatch");
        }
        for(unsigned i=0;i<5;++i) {
            auto held=transition(false,false);
            if(!held.valid || held.result!=expected)throw std::runtime_error("held response changed");
        }
        if(!transition(false,true).valid)throw std::runtime_error("response disappeared before consume");
        if(transition(false,false).valid)throw std::runtime_error("consumed response repeated");

        responseDelay=100;
        if(!transition(true,false).ready)throw std::runtime_error("second command not accepted");
        for(unsigned i=0;i<20 && requests<2;++i)transition(false,false);
        if(requests!=2 || responses!=1)throw std::runtime_error("second read was not pending");
        dut.reset=1;due.clear();edge();dut.reset=0;
        for(unsigned i=0;i<12;++i) {
            auto s=transition(false,false);
            if(s.valid || requests!=2)throw std::runtime_error("reset leaked pending read/atomic result");
        }
        std::printf("PASS nested memory atomic read/stall/reset (%u clocks, %u requests)\n",clocks,requests);
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL at clock %u: %s\n",clocks,error.what());return 1;
    }
}
