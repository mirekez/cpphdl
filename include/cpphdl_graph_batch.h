#pragma once

#include "cpphdl_graph_threads.h"
#include <algorithm>
#include <functional>
#include <type_traits>

namespace cpphdl::graph_runtime {

// Outer parallelism for independent models or independent graph transactions.
// The caller owns the synchronization boundary: jobs must not read or write
// another job's live storage. Run a model's inner evaluator serially here.
// Every job finishes before run returns, including when a job throws.
class Batch {
public:
    enum class Schedule { Static, Dynamic };
    explicit Batch(unsigned threads, std::shared_ptr<Threads> executor = {})
        : threads_(checked(threads)), pool_(executor ? std::move(executor) : std::make_shared<Threads>(threads_, 1)) {}

    template<class Function>
    void run(std::size_t count, Function&& function, Schedule schedule = Schedule::Static,
             std::size_t grain = 1) {
        if (!grain) throw std::invalid_argument("native batch grain must be positive");
        if (!count) return;
        const unsigned active = unsigned(std::min<std::size_t>(threads_, count));
        struct Context {
            std::remove_reference_t<Function>* function;
            std::size_t count, grain;
            unsigned active;
            Schedule schedule;
            std::atomic<std::size_t> next{0};
            std::mutex errorMutex;
            std::exception_ptr error;
            void invoke(std::size_t job) noexcept {
                try { std::invoke(*function, job); }
                catch (...) {
                    std::lock_guard lock(errorMutex);
                    if (!error) error = std::current_exception();
                }
            }
        } context{&function, count, grain, active, schedule};
        pool_->run(&context, [](void* pointer, Threads&, unsigned lane, uint32_t) noexcept {
            auto& c = *static_cast<Context*>(pointer);
            if (c.schedule == Schedule::Static) {
                const auto width = c.count / c.active, extra = c.count % c.active;
                const auto first = width * lane + std::min<std::size_t>(lane, extra);
                const auto end = first + width + (lane < extra);
                for (auto job = first; job < end; ++job) c.invoke(job);
            } else {
                for (;;) {
                    auto first = c.next.load(std::memory_order_relaxed);
                    std::size_t end;
                    do {
                        if (first >= c.count) return;
                        end = first + std::min(c.grain, c.count - first);
                    } while (!c.next.compare_exchange_weak(first, end, std::memory_order_relaxed));
                    for (auto job = first; job < end; ++job) c.invoke(job);
                }
            }
        }, active);
        if (context.error) std::rethrow_exception(context.error);
    }
private:
    static unsigned checked(unsigned threads) {
        if (!threads || threads > 256) throw std::invalid_argument("native batch threads must be 1..256");
        return threads;
    }
    unsigned threads_;
    std::shared_ptr<Threads> pool_;
};
}
