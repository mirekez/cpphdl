#pragma once

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "cpphdl_graph_affinity.h"
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
#include "cpphdl_graph_profile.h"
#endif

namespace cpphdl::graph_runtime {

enum class DispatchPolicy { Automatic, Serial, Parallel };
// Read once when a memoized model is constructed. This controls host
// scheduling only; every mode evaluates the same graph transactions.
inline DispatchPolicy nativeDispatchPolicy() {
    const char* value = std::getenv("CPPHDL_NATIVE_DISPATCH");
    if (!value || std::string_view(value) == "auto") return DispatchPolicy::Automatic;
    if (std::string_view(value) == "serial") return DispatchPolicy::Serial;
    if (std::string_view(value) == "parallel") return DispatchPolicy::Parallel;
    throw std::invalid_argument("CPPHDL_NATIVE_DISPATCH must be auto, serial or parallel");
}

// Read once per executor, before starting workers. CPPHDL_NATIVE_SPIN takes
// precedence over CPPHDL_NATIVE_SPIN_LIMIT; each setting retains its supported
// range. Zero skips our spin window and goes directly to atomic waiting.
inline unsigned nativeSpinLimit() {
    const char* value = std::getenv("CPPHDL_NATIVE_SPIN");
    const bool extended = !value;
    if (extended) value = std::getenv("CPPHDL_NATIVE_SPIN_LIMIT");
    if (!value) return 4096;
    const std::string_view text(value);
    unsigned count = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
    if (error != std::errc{} || end != text.data() + text.size() || count > (extended ? 1000000u : 65536u))
        throw std::invalid_argument(extended
            ? "CPPHDL_NATIVE_SPIN_LIMIT must be an integer from 0 to 1000000"
            : "CPPHDL_NATIVE_SPIN must be an integer from 0 to 65536");
    return count;
}

// Independent tasks can use fewer workers than the compiled pool. Fixed-lane
// dependency stages must keep all their lanes and deliberately ignore this cap.
inline unsigned nativeWorkerLimit(const char* setting, unsigned capacity) {
    const char* value = std::getenv(setting);
    if (!value) return capacity;
    const std::string_view text(value);
    unsigned count = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
    if (error != std::errc{} || end != text.data() + text.size() || !count || count > 256)
        throw std::invalid_argument(std::string(setting)+" must be an integer from 1 to 256");
    return std::min(capacity, count);
}
inline unsigned nativeTaskWorkers(unsigned capacity) {
    return nativeWorkerLimit("CPPHDL_NATIVE_TASK_WORKERS",capacity);
}
inline unsigned nativeMemoryWorkers(unsigned capacity) {
    return nativeWorkerLimit("CPPHDL_NATIVE_MEMORY_WORKERS",capacity);
}

// Persistent workers for one graph transaction. Generated tasks are noexcept:
// validation errors are reported to the caller. A caller continuation may
// consume completed output ports while workers prepare inactive registers.
// RAM writes and state-bank publication wait for every lane to finish. Copies
// may share this executor; dispatches serialize, but model storage stays separate.
// 32-bit epochs let platforms with a 32-bit futex wait on each slot directly,
// avoiding a shared proxy counter on every notification. Wraparound is safe:
// Each worker has its own command/completion stamp, so workers omitted from
// many transactions can rejoin even after the shared transaction epoch wraps.
class Threads {
    struct alignas(64) Slot { std::atomic<uint32_t> value{0}; };
public:
    using Function = void (*)(void*, Threads&, unsigned, uint32_t) noexcept;
    Threads(unsigned lanes, unsigned tasks)
        : lanes_(lanes), spinLimit_(nativeSpinLimit()), commands_(new Slot[lanes]),
          completed_(new Slot[lanes]), tasks_(new Slot[tasks]) {
        try {
            for (unsigned lane = 1; lane < lanes; ++lane) {
                workers_.emplace_back([this, lane] { worker(lane); });
                NativeAffinityScope::bindWorker(workers_.back());
            }
        } catch (...) { stop(); throw; }
    }
    ~Threads() {
        stop();
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
        if(profile_)profile_->report(stderr,this);
#endif
    }
    Threads(const Threads&) = delete;
    Threads& operator=(const Threads&) = delete;
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
    // Inspect only while no caller is dispatching through this executor.
    const DispatchProfile* dispatchProfile() const noexcept { return profile_.get(); }
#endif

    void run(void* model, Function function) {
        run(model, function, lanes_, [] {});
    }
    void run(void* model, Function function, unsigned activeLanes) {
        run(model, function, activeLanes, [] {});
    }
    // Independent generated evaluation groups can outnumber physical workers.
    // Group zero stays on the caller (foreign effects may be pinned there).
    // Each group has private scratch/next-state storage; publication still
    // happens only after run() joins every worker. No cross-group waits are
    // allowed in this entry point.
    void runTasks(void* model, Function function, unsigned count) {
        runTasks(model, function, count, lanes_);
    }
    void runTasks(void* model, Function function, unsigned count, unsigned requestedWorkers) {
        if (!count) return;
        if (!requestedWorkers) throw std::invalid_argument("native task worker budget must be positive");
        const unsigned active = std::min({taskWorkers_, count, requestedWorkers});
        if (active == count) {
            run(model, function, active, [] {}
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
                ,function,count,true
#endif
            );
            return;
        }
        struct Work {
            void* model;
            Function function;
            unsigned count;
            std::atomic<unsigned> next;
        } work{model, function, count, active};
        if (active == 1) {
            run(&work, [](void* opaque, Threads& executor, unsigned, uint32_t epoch) noexcept {
                auto& work = *static_cast<Work*>(opaque);
                for (unsigned task = 0; task < work.count; ++task) {
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
                    executor.profileItem(0);
#endif
                    work.function(work.model, executor, task, epoch);
                }
            }, 1u, [] {}
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
                ,function,count,true
#endif
            );
            return;
        }
        run(&work, [](void* opaque, Threads& executor, unsigned lane, uint32_t epoch) noexcept {
            auto& work = *static_cast<Work*>(opaque);
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
            executor.profileItem(lane);
#endif
            work.function(work.model, executor, lane, epoch);
            for (;;) {
                const unsigned task = work.next.fetch_add(1, std::memory_order_relaxed);
                if (task >= work.count) break;
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
                executor.profileItem(lane);
#endif
                work.function(work.model, executor, task, epoch);
            }
        }, active, [] {}
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
            ,function,count,true
#endif
        );
    }
    // Run a caller-only continuation before joining the register workers.
    // Always drain workers before propagating an exception or releasing the
    // shared executor; model storage must never outlive an unfinished job.
    template<class Continuation>
    void run(void* model, Function function, Continuation&& continuation) {
        run(model, function, lanes_, std::forward<Continuation>(continuation));
    }
    template<class Continuation>
    void run(void* model, Function function, unsigned activeLanes, Continuation&& continuation
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
             ,Function identity=nullptr, unsigned taskCount=0, bool independent=false
#endif
    ) {
        if (!activeLanes || activeLanes > lanes_)
            throw std::invalid_argument("native active lanes exceed executor capacity");
        std::lock_guard lock(mutex_);
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
        if(profile_)profile_->begin({reinterpret_cast<uintptr_t>(identity?identity:function),activeLanes,
                                     taskCount?taskCount:activeLanes,independent});
#endif
        model_ = model; function_ = function;
        const auto epoch = ++epoch_;
        for (unsigned lane = 1; lane < activeLanes; ++lane) {
            commands_[lane].value.fetch_add(1, std::memory_order_release);
            commands_[lane].value.notify_one();
        }
        invoke(function,model,0,epoch);
        std::exception_ptr error;
        try { continuation(); } catch (...) { error = std::current_exception(); }
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
        if(profile_)profile_->continuationDone();
#endif
        for (unsigned lane = 1; lane < activeLanes; ++lane)
            waitFor(completed_[lane].value, commands_[lane].value.load(std::memory_order_relaxed));
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
        if(profile_)profile_->finish();
#endif
        if (error) std::rethrow_exception(error);
    }
    void wait(unsigned task, uint32_t epoch) const noexcept {
        waitFor(tasks_[task].value, epoch);
    }
    void complete(unsigned task, uint32_t epoch) noexcept {
        tasks_[task].value.store(epoch, std::memory_order_release);
        tasks_[task].value.notify_all();
    }
private:
    void invoke(Function function, void* model, unsigned lane, uint32_t epoch) noexcept {
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
        if(profile_ && profile_->sampled()) {
            profile_->enter(lane);
            function(model,*this,lane,epoch);
            profile_->leave(lane);
            return;
        }
#endif
        function(model,*this,lane,epoch);
    }
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
    void profileItem(unsigned lane) noexcept { if(profile_)profile_->item(lane); }
#endif
    static void pause() noexcept {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        asm volatile("yield");
#else
        std::this_thread::yield();
#endif
    }
    void waitFor(const std::atomic<uint32_t>& slot, uint32_t epoch) const noexcept {
        unsigned spins = 0;
        for (;;) {
            auto observed = slot.load(std::memory_order_acquire);
            if (observed == epoch) return;
            if (++spins < spinLimit_) pause();
            else { slot.wait(observed, std::memory_order_acquire); spins = 0; }
        }
    }
    void worker(unsigned lane) noexcept {
        uint32_t seen = 0;
        auto& command = commands_[lane].value;
        for (;;) {
            auto epoch = command.load(std::memory_order_acquire);
            if (epoch == seen) {
                unsigned spins = 0;
                do {
                    if (++spins < spinLimit_) pause();
                    else { command.wait(seen, std::memory_order_acquire); spins = 0; }
                    epoch = command.load(std::memory_order_acquire);
                } while (epoch == seen);
            }
            if (stopping_.load(std::memory_order_relaxed)) return;
            invoke(function_,model_,lane,epoch_);
            completed_[lane].value.store(epoch, std::memory_order_release);
            completed_[lane].value.notify_all();
            seen = epoch;
        }
    }
    void stop() noexcept {
        stopping_.store(true, std::memory_order_relaxed);
        for (unsigned lane = 1; lane < lanes_; ++lane) {
            commands_[lane].value.fetch_add(1, std::memory_order_release);
            commands_[lane].value.notify_one();
        }
        for (auto& worker : workers_) worker.join();
    }
    unsigned lanes_;
    const unsigned spinLimit_;
    const unsigned taskWorkers_ = nativeTaskWorkers(lanes_);
    std::unique_ptr<Slot[]> commands_, completed_, tasks_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    void* model_ = nullptr;
    Function function_ = nullptr;
    uint32_t epoch_ = 0;
    std::atomic<bool> stopping_{false};
#ifdef CPPHDL_NATIVE_DISPATCH_PROFILE
    std::unique_ptr<DispatchProfile> profile_=nativeDispatchProfile(lanes_);
#endif
};
}
