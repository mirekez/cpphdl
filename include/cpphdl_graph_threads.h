#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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
        : lanes_(lanes), commands_(new Slot[lanes]), completed_(new Slot[lanes]), tasks_(new Slot[tasks]) {
        try {
            for (unsigned lane = 1; lane < lanes; ++lane)
                workers_.emplace_back([this, lane] { worker(lane); });
        } catch (...) { stop(); throw; }
    }
    ~Threads() { stop(); }
    Threads(const Threads&) = delete;
    Threads& operator=(const Threads&) = delete;

    void run(void* model, Function function) {
        run(model, function, lanes_, [] {});
    }
    void run(void* model, Function function, unsigned activeLanes) {
        run(model, function, activeLanes, [] {});
    }
    // Run a caller-only continuation before joining the register workers.
    // Always drain workers before propagating an exception or releasing the
    // shared executor; model storage must never outlive an unfinished job.
    template<class Continuation>
    void run(void* model, Function function, Continuation&& continuation) {
        run(model, function, lanes_, std::forward<Continuation>(continuation));
    }
    template<class Continuation>
    void run(void* model, Function function, unsigned activeLanes, Continuation&& continuation) {
        if (!activeLanes || activeLanes > lanes_)
            throw std::invalid_argument("native active lanes exceed executor capacity");
        std::lock_guard lock(mutex_);
        model_ = model; function_ = function;
        const auto epoch = ++epoch_;
        for (unsigned lane = 1; lane < activeLanes; ++lane) {
            commands_[lane].value.fetch_add(1, std::memory_order_release);
            commands_[lane].value.notify_one();
        }
        function(model, *this, 0, epoch);
        std::exception_ptr error;
        try { continuation(); } catch (...) { error = std::current_exception(); }
        for (unsigned lane = 1; lane < activeLanes; ++lane)
            waitFor(completed_[lane].value, commands_[lane].value.load(std::memory_order_relaxed));
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
    // Long idle spins can keep the caller descheduled on a busy host. A short
    // window catches immediate completions, then lets the futex wake the lane.
    static constexpr unsigned spinLimit = 64;
    static void pause() noexcept {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        asm volatile("yield");
#else
        std::this_thread::yield();
#endif
    }
    static void waitFor(const std::atomic<uint32_t>& slot, uint32_t epoch) noexcept {
        unsigned spins = 0;
        for (;;) {
            auto observed = slot.load(std::memory_order_acquire);
            if (observed == epoch) return;
            if (++spins < spinLimit) pause();
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
                    if (++spins < spinLimit) pause();
                    else { command.wait(seen, std::memory_order_acquire); spins = 0; }
                    epoch = command.load(std::memory_order_acquire);
                } while (epoch == seen);
            }
            if (stopping_.load(std::memory_order_relaxed)) return;
            function_(model_, *this, lane, epoch_);
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
    std::unique_ptr<Slot[]> commands_, completed_, tasks_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    void* model_ = nullptr;
    Function function_ = nullptr;
    uint32_t epoch_ = 0;
    std::atomic<bool> stopping_{false};
};
}
