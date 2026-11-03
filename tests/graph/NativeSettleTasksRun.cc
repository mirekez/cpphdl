#include "model.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string_view>
#include <thread>
#ifndef NATIVE_SETTLE_ROUNDS
#define NATIVE_SETTLE_ROUNDS 32
#endif
using Model=cpphdl_native::Model;
void check(Model& model,unsigned seed) {
    std::array<uint32_t,24> expected{};
    for(unsigned tick=0;tick<300;++tick) {
        model.data[0]=seed+tick*7919;model.reset[0]=tick%77==0;
        if(tick%19==0) {
            model.bad[0]=1;
            try {model.eval(true);assert(false);}catch(const std::out_of_range&){}
            // A rejected commit must not poison output-only evaluation.
            model.eval(false);assert(model.observed[0]==expected[0]);
            model.bad[0]=0;
            model.continuous_bad[0]=1;
            try {model.eval(false);assert(false);}catch(const std::out_of_range&){}
            model.continuous_bad[0]=0;
        }
        auto old=expected;
        for(unsigned i=0;i<24;++i) {
            uint32_t value=(old[i]^model.data[0])+old[(i+1)%24];
            for(unsigned k=0;k<NATIVE_SETTLE_ROUNDS;++k) {value^=value<<13;value^=value>>17;value^=value<<5;}
            expected[i]=model.reset[0]?0:value;
        }
        model.step();assert(model.observed[0]==expected[0]);
        model.eval(false);assert(model.observed[0]==expected[0]);
        if(tick%31==0) {auto copy=model;model=copy;model.eval(false);assert(model.observed[0]==expected[0]);}
    }
}
int main(int argc,char** argv) {
#ifdef EXPECT_FILTERED
    static_assert(Model::__cpphdl_settle_task_count<Model::__cpphdl_task_count);
#endif
    if(argc==1) {
        Model model;check(model,1);
        Model seed,left=seed,right=seed;
        std::thread a([&]{check(left,17);}),b([&]{check(right,29);});a.join();b.join();
        std::puts("PASS settle tasks: oracle, reset, continuous checks, rejected commits, copies");return 0;
    }
    Model model;uint64_t clocks=0,digest=0;
    const bool step=std::string_view(argv[1])=="step";
    const auto startCpu=std::clock();
    auto start=std::chrono::steady_clock::now();double seconds;
    do {
        for(unsigned i=0;i<1024;++i) {
            model.data[0]=i*7919;
            if(step)model.step();else model.eval(false);
            digest=digest*131+model.observed[0];
        }
        clocks+=1024;seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    }while(seconds<2);
    std::printf("{\"evaluations\":%llu,\"seconds\":%.9f,\"cpu_seconds\":%.9f,\"settle_tasks\":%u,\"tasks\":%u,\"digest\":\"%llx\"}\n",
        (unsigned long long)clocks,seconds,double(std::clock()-startCpu)/CLOCKS_PER_SEC,
        Model::__cpphdl_settle_task_count,Model::__cpphdl_task_count,(unsigned long long)digest);
}
