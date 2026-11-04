#define CPPHDL_NATIVE_DISPATCH_PROFILE
#include "cpphdl_graph_threads.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

using cpphdl::graph_runtime::Threads;
static void require(bool condition) { if(!condition)std::abort(); }
struct Work { std::array<std::atomic<unsigned>,17> visits{}; };
static void task(void* opaque,Threads&,unsigned index,uint32_t) noexcept {
    ++static_cast<Work*>(opaque)->visits[index];
}
static void fixed(void* opaque,Threads&,unsigned index,uint32_t) noexcept {
    ++static_cast<Work*>(opaque)->visits[index];
}
static void check(unsigned every) {
    const auto setting=std::to_string(every);
    setenv("CPPHDL_NATIVE_PROFILE_EVERY",setting.c_str(),1);
    unsetenv("CPPHDL_NATIVE_TASK_WORKERS");
    Work work;
    Threads pool(4,32);
    for(unsigned round=0;round<64;++round) {
        pool.runTasks(&work,task,17);
        pool.runTasks(&work,task,17,1);
        pool.runTasks(&work,task,4);
        bool caught=false;
        try { pool.run(&work,fixed,4,[]{throw 71;}); }
        catch(int e) { caught=e==71; }
        require(caught);
    }
    for(unsigned i=0;i<17;++i)require(work.visits[i]==(i<4?256u:128u));
    const auto* profile=pool.dispatchProfile();
    if(!every){require(!profile);return;}
    require(profile && profile->stats().size()==4);
    for(const auto& [key,s]:profile->stats()) {
        require(s.calls==64 && s.samples>0 && s.samples<=s.calls);
        require(every!=1 || s.samples==s.calls);
        require(s.items==s.samples*key.tasks);
        require(s.sampledElapsed==s.wake+s.caller+s.continuation+s.join);
        require(s.elapsed>=s.sampledElapsed);
        require(s.service>=s.minService*key.workers);
        require(s.service<=s.maxService*key.workers);
        require(s.items>=s.minItems*key.workers && s.items<=s.maxItems*key.workers);
        require(key.callback==reinterpret_cast<uintptr_t>(key.independent?task:fixed));
        uint64_t service=0,items=0;
        require(s.lanes.size()==key.workers);
        for(const auto& lane:s.lanes){service+=lane.service;items+=lane.items;}
        require(service==s.service && items==s.items);
    }
}
int main() {
    cpphdl::graph_runtime::NativeAffinityScope affinity;
    check(0);check(1);check(17);
    for(const char* value:{"-1","garbage","1000000001",""}) {
        setenv("CPPHDL_NATIVE_PROFILE_EVERY",value,1);
        bool rejected=false;
        try { Threads pool(2,2); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected);
    }
    std::puts("PASS dispatch profile: task accounting, phase sums, sampling, exceptions, disabled mode");
}
