#pragma once

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace cpphdl::graph_runtime {

enum class NativeAffinityPolicy { None, Caller };
inline NativeAffinityPolicy nativeAffinityPolicy() {
    const char* value=std::getenv("CPPHDL_NATIVE_AFFINITY");
    if(!value || std::string_view(value)=="none")return NativeAffinityPolicy::None;
    if(std::string_view(value)=="caller")return NativeAffinityPolicy::Caller;
    throw std::invalid_argument("CPPHDL_NATIVE_AFFINITY must be none or caller");
}

#ifdef __linux__
namespace affinity_detail {
inline cpu_set_t currentMask() {
    cpu_set_t mask{};
    const int error=pthread_getaffinity_np(pthread_self(),sizeof(mask),&mask);
    if(error)throw std::system_error(error,std::generic_category(),"native caller affinity query");
    return mask;
}
// Parse Linux topology lists, including non-contiguous SMT siblings.
inline cpu_set_t parseCpuList(std::string_view text) {
    cpu_set_t result{};
    auto number=[&]() {
        unsigned value=0;
        const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
        if(error!=std::errc{} || end==text.data() || value>=CPU_SETSIZE)
            throw std::runtime_error("invalid or unsupported native CPU topology list");
        text.remove_prefix(end-text.data());
        return value;
    };
    if(text.empty())throw std::runtime_error("empty native CPU topology list");
    for(;;) {
        const unsigned first=number();
        unsigned last=first;
        if(!text.empty() && text.front()=='-') {text.remove_prefix(1);last=number();}
        if(last<first)throw std::runtime_error("reversed native CPU topology range");
        for(unsigned cpu=first;cpu<=last;++cpu)CPU_SET(cpu,&result);
        if(text.empty())break;
        if(text.front()!=',')throw std::runtime_error("invalid native CPU topology separator");
        text.remove_prefix(1);
    }
    return result;
}
inline cpu_set_t siblings(unsigned cpu) {
    std::ifstream file("/sys/devices/system/cpu/cpu"+std::to_string(cpu)+"/topology/thread_siblings_list");
    std::string line;
    if(!std::getline(file,line))throw std::runtime_error("cannot read native CPU sibling topology");
    auto mask=parseCpuList(line);
    if(!CPU_ISSET(cpu,&mask))throw std::runtime_error("native sibling topology omits caller CPU");
    return mask;
}
inline cpu_set_t workerMask(const cpu_set_t& allowed,const cpu_set_t& siblings) {
    cpu_set_t result=allowed;
    for(unsigned cpu=0;cpu<CPU_SETSIZE;++cpu)
        if(CPU_ISSET(cpu,&siblings))CPU_CLR(cpu,&result);
    if(!CPU_COUNT(&result))throw std::runtime_error("native caller isolation requires a second allowed physical core");
    return result;
}
}
#endif

// Place this scope before constructing native models, and destroy their pools
// before leaving it. The calling thread stays pinned through serial publication
// and host gaps. No affinity syscall is performed during graph dispatch.
// Scopes are thread-bound and nest in stack order; they are not model state.
class NativeAffinityScope {
public:
    explicit NativeAffinityScope(NativeAffinityPolicy policy=nativeAffinityPolicy()) {
        if(policy==NativeAffinityPolicy::None)return;
#ifdef __linux__
        original_=affinity_detail::currentMask();
        // A nested scope inherits the outer worker mask instead of treating
        // the already-pinned caller mask as the entire CPU allocation.
        if(current_ && CPU_EQUAL(&original_,&current_->caller_)) {
            caller_=current_->caller_;workers_=current_->workers_;cpu_=current_->cpu_;
        } else {
            for(unsigned cpu=0;cpu<CPU_SETSIZE;++cpu)
                if(CPU_ISSET(cpu,&original_)) {cpu_=int(cpu);break;}
            if(cpu_<0)throw std::runtime_error("native caller has no allowed CPU");
            workers_=affinity_detail::workerMask(original_,affinity_detail::siblings(unsigned(cpu_)));
            CPU_SET(cpu_,&caller_);
        }
        const int error=pthread_setaffinity_np(pthread_self(),sizeof(caller_),&caller_);
        if(error)throw std::system_error(error,std::generic_category(),"native caller pinning");
        owner_=std::this_thread::get_id();previous_=current_;current_=this;enabled_=true;
#else
        throw std::runtime_error("native caller isolation requires Linux CPU affinity support");
#endif
    }
    ~NativeAffinityScope() noexcept {
        if(!enabled_)return;
        // Restoring another thread, or unwinding scopes out of order, could
        // silently leave a dangling thread-local placement policy.
        if(owner_!=std::this_thread::get_id() || current_!=this)std::terminate();
#ifdef __linux__
        const int error=pthread_setaffinity_np(pthread_self(),sizeof(original_),&original_);
        if(error)std::fprintf(stderr,"cpphdl: restoring caller affinity failed (error %d)\n",error);
#endif
        current_=previous_;
    }
    NativeAffinityScope(const NativeAffinityScope&)=delete;
    NativeAffinityScope& operator=(const NativeAffinityScope&)=delete;
    bool enabled() const noexcept {return enabled_;}
    int callerCpu() const noexcept {return cpu_;}

    // Called by Threads on its constructing thread. Workers initially inherit
    // the caller-only mask; widen them to the saved allocation minus siblings
    // before the executor can dispatch its first job. Failure aborts setup.
    static void bindWorker(std::thread& worker) {
#ifdef __linux__
        if(!current_)return;
        const int error=pthread_setaffinity_np(worker.native_handle(),sizeof(current_->workers_),&current_->workers_);
        if(error)throw std::system_error(error,std::generic_category(),"native worker pinning");
#else
        (void)worker;
#endif
    }
private:
    inline static thread_local NativeAffinityScope* current_=nullptr;
    NativeAffinityScope* previous_=nullptr;
    std::thread::id owner_{};
    bool enabled_=false;
    int cpu_=-1;
#ifdef __linux__
    cpu_set_t original_{},caller_{},workers_{};
#endif
};
}
