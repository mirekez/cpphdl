#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cpphdl::graph_runtime {

// Persistent workers for one graph transaction. Generated tasks are noexcept:
// validation errors are reported to the caller. A caller continuation may
// consume completed output ports while workers prepare inactive registers.
// RAM writes and state-bank publication wait for every lane to finish. Copies
// may share this executor; dispatches serialize, but model storage stays separate.
// 32-bit epochs let platforms with a 32-bit futex wait on each slot directly,
// avoiding a shared proxy counter on every notification. Wraparound is safe:
// run() joins every lane before advancing, so all live stamps are one epoch old.
class Threads {
    struct alignas(64) Slot { std::atomic<uint32_t> value{0}; };
public:
    using Function = void (*)(void*, Threads&, unsigned, uint32_t) noexcept;
    Threads(unsigned lanes, unsigned tasks)
        : lanes_(lanes), completed_(new Slot[lanes]), tasks_(new Slot[tasks]) {
        try {
            for (unsigned lane = 1; lane < lanes; ++lane)
                workers_.emplace_back([this, lane] { worker(lane); });
        } catch (...) { stop(); throw; }
    }
    ~Threads() { stop(); }
    Threads(const Threads&) = delete;
    Threads& operator=(const Threads&) = delete;

    void run(void* model, Function function) {
        run(model, function, [] {});
    }
    // Run a caller-only continuation before joining the register workers.
    // Always drain workers before propagating an exception or releasing the
    // shared executor; model storage must never outlive an unfinished job.
    template<class Continuation>
    void run(void* model, Function function, Continuation&& continuation) {
        std::lock_guard lock(mutex_);
        model_ = model; function_ = function;
        const auto epoch = ++epoch_;
        command_.store(epoch, std::memory_order_release);
        command_.notify_all();
        function(model, *this, 0, epoch);
        std::exception_ptr error;
        try { continuation(); } catch (...) { error = std::current_exception(); }
        for (unsigned lane = 1; lane < lanes_; ++lane)
            waitFor(completed_[lane].value, epoch);
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
            if (++spins < 4096) pause();
            else { slot.wait(observed, std::memory_order_acquire); spins = 0; }
        }
    }
    void worker(unsigned lane) noexcept {
        uint32_t seen = 0;
        for (;;) {
            auto epoch = command_.load(std::memory_order_acquire);
            if (epoch == seen) {
                unsigned spins = 0;
                do {
                    if (++spins < 4096) pause();
                    else { command_.wait(seen, std::memory_order_acquire); spins = 0; }
                    epoch = command_.load(std::memory_order_acquire);
                } while (epoch == seen);
            }
            if (stopping_.load(std::memory_order_relaxed)) return;
            function_(model_, *this, lane, epoch);
            completed_[lane].value.store(epoch, std::memory_order_release);
            completed_[lane].value.notify_all();
            seen = epoch;
        }
    }
    void stop() noexcept {
        stopping_.store(true, std::memory_order_relaxed);
        command_.fetch_add(1, std::memory_order_release);
        command_.notify_all();
        for (auto& worker : workers_) worker.join();
    }
    unsigned lanes_;
    std::unique_ptr<Slot[]> completed_, tasks_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    void* model_ = nullptr;
    Function function_ = nullptr;
    uint32_t epoch_ = 0;
    alignas(64) std::atomic<uint32_t> command_{0};
    std::atomic<bool> stopping_{false};
};
}
