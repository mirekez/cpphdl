#pragma once

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

namespace cpphdl::graph_runtime {

// Optional executor diagnostics, compiled only with
// CPPHDL_NATIVE_DISPATCH_PROFILE. Callback times are elapsed host times,
// including preemption and dependency/queue waits, not useful CPU cycles.
class DispatchProfile {
public:
    struct Key {
        uintptr_t callback;
        unsigned workers, tasks;
        bool independent;
        bool operator<(const Key& b) const noexcept {
            return std::tie(callback,workers,tasks,independent) <
                   std::tie(b.callback,b.workers,b.tasks,b.independent);
        }
    };
    struct LaneTotals { uint64_t startDelay=0, service=0, items=0; };
    struct Stats {
        uint64_t calls=0, samples=0, elapsed=0, gap=0;
        uint64_t sampledElapsed=0, wake=0, caller=0, continuation=0, join=0;
        uint64_t service=0, minService=0, maxService=0, maxStartDelay=0;
        uint64_t items=0, minItems=0, maxItems=0;
        std::vector<LaneTotals> lanes;
    };
    struct alignas(64) Lane {
        uint64_t begin=0, end=0, items=0;
    };
    static uint64_t now() noexcept {
        return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    explicit DispatchProfile(unsigned capacity, unsigned every)
        : lanes_(capacity), every_(every) {}
    bool sampled() const noexcept { return sampled_; }
    void begin(Key key) {
        // All aggregate bookkeeping is caller-owned under the executor lock.
        // Allocation happens only when a new callback/shape is first seen.
        current_=&stats_[key]; key_=key;
        if(current_->lanes.empty())current_->lanes.resize(key.workers);
        const auto n=++current_->calls;
        // Scramble each key's call index rather than sample every Nth global
        // dispatch, which can alias periodic graph phases/activity patterns.
        auto random=n+0x9e3779b97f4a7c15ull;
        random=(random^(random>>30))*0xbf58476d1ce4e5b9ull;
        random=(random^(random>>27))*0x94d049bb133111ebull;
        random^=random>>31;
        sampled_=n==1 || random%every_==0;
        if(sampled_)for(unsigned i=0;i<key.workers;++i)
            lanes_[i]={0,0,key.tasks==key.workers?1u:0u};
        begin_=now();
        if(previousEnd_)current_->gap+=begin_-previousEnd_;
    }
    void enter(unsigned lane) noexcept { lanes_[lane].begin=now(); }
    void leave(unsigned lane) noexcept { lanes_[lane].end=now(); }
    void item(unsigned lane) noexcept { if(sampled_)++lanes_[lane].items; }
    void continuationDone() noexcept { if(sampled_)continuationEnd_=now(); }
    void finish() noexcept {
        const auto end=now();
        current_->elapsed+=end-begin_;
        if(sampled_) {
            auto& s=*current_; ++s.samples; s.sampledElapsed+=end-begin_;
            s.wake+=lanes_[0].begin-begin_;
            s.caller+=lanes_[0].end-lanes_[0].begin;
            s.continuation+=continuationEnd_-lanes_[0].end;
            s.join+=end-continuationEnd_;
            uint64_t minService=UINT64_MAX,maxService=0,maxStart=0;
            uint64_t minItems=UINT64_MAX,maxItems=0;
            for(unsigned i=0;i<key_.workers;++i) {
                const auto& lane=lanes_[i];
                const auto duration=lane.end-lane.begin;
                s.service+=duration; s.items+=lane.items;
                s.lanes[i].startDelay+=lane.begin-begin_;
                s.lanes[i].service+=duration;s.lanes[i].items+=lane.items;
                minService=std::min(minService,duration);maxService=std::max(maxService,duration);
                maxStart=std::max(maxStart,lane.begin-begin_);
                minItems=std::min(minItems,lane.items);maxItems=std::max(maxItems,lane.items);
            }
            s.minService+=minService;s.maxService+=maxService;s.maxStartDelay+=maxStart;
            s.minItems+=minItems;s.maxItems+=maxItems;
        }
        // Exclude aggregation time from the next caller-gap measurement.
        previousEnd_=now();
    }
    const auto& stats() const noexcept { return stats_; }
    void report(FILE* output, const void* executor) const noexcept {
        for(const auto& [key,s]:stats_) {
            std::fprintf(output,
                "cpphdl_dispatch_profile executor=%p callback=0x%llx anchor=0x%llx kind=%s workers=%u tasks=%u "
                "calls=%llu samples=%llu elapsed_ns=%llu gap_before_ns=%llu "
                "sampled_elapsed_ns=%llu wake_ns=%llu caller_ns=%llu continuation_ns=%llu join_ns=%llu "
                "service_ns=%llu min_service_ns=%llu max_service_ns=%llu max_start_delay_ns=%llu "
                "items=%llu min_items=%llu max_items=%llu\n",
                executor,(unsigned long long)key.callback,(unsigned long long)reinterpret_cast<uintptr_t>(&now),
                key.independent?"tasks":"fixed",key.workers,key.tasks,
                (unsigned long long)s.calls,(unsigned long long)s.samples,(unsigned long long)s.elapsed,
                (unsigned long long)s.gap,(unsigned long long)s.sampledElapsed,(unsigned long long)s.wake,
                (unsigned long long)s.caller,(unsigned long long)s.continuation,(unsigned long long)s.join,
                (unsigned long long)s.service,(unsigned long long)s.minService,(unsigned long long)s.maxService,
                (unsigned long long)s.maxStartDelay,(unsigned long long)s.items,
                (unsigned long long)s.minItems,(unsigned long long)s.maxItems);
            for(unsigned i=0;i<key.workers;++i) {
                const auto& lane=s.lanes[i];
                std::fprintf(output,
                    "cpphdl_dispatch_lane executor=%p callback=0x%llx anchor=0x%llx kind=%s workers=%u tasks=%u "
                    "lane=%u samples=%llu start_delay_ns=%llu service_ns=%llu items=%llu\n",
                    executor,(unsigned long long)key.callback,(unsigned long long)reinterpret_cast<uintptr_t>(&now),
                    key.independent?"tasks":"fixed",key.workers,key.tasks,i,(unsigned long long)s.samples,
                    (unsigned long long)lane.startDelay,(unsigned long long)lane.service,(unsigned long long)lane.items);
            }
        }
    }
private:
    std::vector<Lane> lanes_;
    unsigned every_;
    std::map<Key,Stats> stats_;
    Stats* current_=nullptr;
    Key key_{};
    uint64_t begin_=0, previousEnd_=0, continuationEnd_=0;
    bool sampled_=false;
};

inline std::unique_ptr<DispatchProfile> nativeDispatchProfile(unsigned capacity) {
    unsigned every=1024;
    if(const char* value=std::getenv("CPPHDL_NATIVE_PROFILE_EVERY")) {
        const std::string_view text(value);
        const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),every);
        if(error!=std::errc{} || end!=text.data()+text.size() || every>1000000000)
            throw std::invalid_argument("CPPHDL_NATIVE_PROFILE_EVERY must be from 0 to 1000000000");
    }
    if(!every)return {};
    return std::make_unique<DispatchProfile>(capacity,every);
}
}
