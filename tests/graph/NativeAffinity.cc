#include "cpphdl_graph_threads.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>

using namespace cpphdl::graph_runtime;
static void require(bool value) {if(!value)std::abort();}
#ifdef __linux__
static bool same(const cpu_set_t& a,const cpu_set_t& b) {return CPU_EQUAL(&a,&b);}
static void setMask(const cpu_set_t& mask) {
    require(!pthread_setaffinity_np(pthread_self(),sizeof(mask),&mask));
}
static void masks() {
    const auto allowed=affinity_detail::parseCpuList("2,8,10,12-13");
    const auto siblings=affinity_detail::parseCpuList("2,10");
    const auto workers=affinity_detail::workerMask(allowed,siblings);
    require(same(workers,affinity_detail::parseCpuList("8,12-13")));
    for(const char* bad:{"",",","2,","3-2","1--2","-1","1:2","x","1024","999999999999"}) {
        bool caught=false;
        try {(void)affinity_detail::parseCpuList(bad);}catch(const std::runtime_error&){caught=true;}
        require(caught);
    }
    bool caught=false;
    try {(void)affinity_detail::workerMask(siblings,siblings);}catch(const std::runtime_error&){caught=true;}
    require(caught);
}
static void execute(Threads& pool,const cpu_set_t& caller,const cpu_set_t& workers) {
    struct Work {
        std::thread::id caller;
        cpu_set_t callerMask,workerMask;
        std::array<std::atomic<unsigned>,120> visits{};
    } work{std::this_thread::get_id(),caller,workers};
    auto task=[](void* p,Threads&,unsigned task,uint32_t) noexcept {
        auto& w=*static_cast<Work*>(p);cpu_set_t mask{};
        require(!pthread_getaffinity_np(pthread_self(),sizeof(mask),&mask));
        require(same(mask,std::this_thread::get_id()==w.caller?w.callerMask:w.workerMask));
        ++w.visits[task];
    };
    pool.run(&work,task); // Every fixed lane must observe its assigned mask.
    for(unsigned cap:{1u,2u,4u})pool.runTasks(&work,task,120,cap);
    for(unsigned i=0;i<120;++i)require(work.visits[i]==(i<4?4u:3u));
}
static void live(const cpu_set_t& allocation) {
    setMask(allocation);
    {
        NativeAffinityScope affinity(NativeAffinityPolicy::Caller);
        require(affinity.enabled());
        const auto caller=affinity_detail::currentMask();
        require(CPU_COUNT(&caller)==1 && CPU_ISSET(affinity.callerCpu(),&caller));
        const auto workers=affinity_detail::workerMask(allocation,affinity_detail::siblings(affinity.callerCpu()));
        Threads pool(4,120);execute(pool,caller,workers);
        // Copies can share a pool while dispatching from different callers.
        std::thread a([&]{execute(pool,caller,workers);});
        std::thread b([&]{execute(pool,caller,workers);});a.join();b.join();
        bool caught=false;
        try {
            NativeAffinityScope nested(NativeAffinityPolicy::Caller);
            require(nested.callerCpu()==affinity.callerCpu());
            Threads another(4,120);execute(another,caller,workers);
            another.run(nullptr,[](void*,Threads&,unsigned,uint32_t) noexcept {},4,[]{throw 7;});
        }catch(int value){caught=value==7;}
        require(caught && same(affinity_detail::currentMask(),caller));
        execute(pool,caller,workers);
    }
    require(same(affinity_detail::currentMask(),allocation));
}
#endif
int main() {
    unsetenv("CPPHDL_NATIVE_TASK_WORKERS");
    unsetenv("CPPHDL_NATIVE_AFFINITY");
    require(nativeAffinityPolicy()==NativeAffinityPolicy::None);
    for(const char* value:{"","invalid","CALLER"}) {
        setenv("CPPHDL_NATIVE_AFFINITY",value,1);bool caught=false;
        try {NativeAffinityScope scope;}catch(const std::invalid_argument&){caught=true;}
        require(caught);
    }
    setenv("CPPHDL_NATIVE_AFFINITY","none",1);
    {NativeAffinityScope scope;require(!scope.enabled());}
    setenv("CPPHDL_NATIVE_AFFINITY","caller",1);
    require(nativeAffinityPolicy()==NativeAffinityPolicy::Caller);
#ifdef __linux__
    masks();const auto original=affinity_detail::currentMask();
    int first=-1,other=-1;
    for(int cpu=0;cpu<CPU_SETSIZE;++cpu)if(CPU_ISSET(cpu,&original)){first=cpu;break;}
    require(first>=0);
    const auto siblings=affinity_detail::siblings(first);
    for(int cpu=0;cpu<CPU_SETSIZE;++cpu)
        if(CPU_ISSET(cpu,&original) && !CPU_ISSET(cpu,&siblings)){other=cpu;break;}
    cpu_set_t single{};CPU_SET(first,&single);setMask(single);
    bool caught=false;
    try {NativeAffinityScope scope;}catch(const std::runtime_error&){caught=true;}
    require(caught && same(affinity_detail::currentMask(),single));setMask(original);
    if(other<0) {std::puts("SKIP live caller isolation: only one allowed physical core");return 77;}
    live(original);
    cpu_set_t restricted{};CPU_SET(first,&restricted);CPU_SET(other,&restricted);
    live(restricted);setMask(original);
    {NativeAffinityScope none(NativeAffinityPolicy::None);require(same(affinity_detail::currentMask(),original));}
    std::puts("PASS native affinity: topology, restricted CPUs, worker/task masks, nested scopes, shared callers, exceptions, restoration");
#else
    bool caught=false;
    try {NativeAffinityScope scope;}catch(const std::runtime_error&){caught=true;}
    require(caught);std::puts("PASS native affinity: disabled policy and unsupported platform rejection");
#endif
}
