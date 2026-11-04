#include <cpphdl_graph_threads.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>

using cpphdl::graph_runtime::Threads;

static void require(bool condition) {
    if (!condition) std::abort();
}

// Exercise both small and large pools. A dormant worker must be able to
// rejoin after any number of transactions in which it did not participate.
static void subsets(Threads& pool, unsigned capacity) {
    struct Work {
        unsigned active = 0;
        std::array<unsigned, 30> visits{};
        std::thread::id caller;
    } work;
    std::array<unsigned, 30> expected{};
    work.caller = std::this_thread::get_id();
    for (unsigned i = 0; i < 2048; ++i) {
        work.active = i % 3 == 0 ? capacity : 1 + (i * 17) % capacity;
        for (unsigned j = 0; j < work.active; ++j) ++expected[j];
        pool.run(&work, [](void* ptr, Threads&, unsigned lane, uint32_t) noexcept {
            auto& w = *static_cast<Work*>(ptr);
            require(lane < w.active);
            require((lane == 0) == (std::this_thread::get_id() == w.caller));
            ++w.visits[lane];
        }, work.active);
        require(work.visits == expected);
    }
}

static void tasks(Threads& pool, unsigned capacity) {
    struct Work {
        unsigned count;
        std::array<std::atomic<unsigned>, 120> visits{};
        std::array<std::thread::id, 120> workers{};
        std::thread::id caller = std::this_thread::get_id();
    } work;
    for (unsigned i = 0; i < 512; ++i) {
        work.count = i % 4 ? 120 : i % 31;
        for (auto& v : work.visits) v.store(0);
        auto visit = [](void* ptr, Threads&, unsigned task, uint32_t) noexcept {
            auto& w = *static_cast<Work*>(ptr);
            require(task < w.count);
            if (task == 0) require(std::this_thread::get_id() == w.caller);
            require(w.visits[task].fetch_add(1) == 0);
            w.workers[task] = std::this_thread::get_id();
            if (task % 17 == 0) std::this_thread::yield();
        };
        if (i % 2) pool.runTasks(&work, visit, work.count, 1 + (i % 30));
        else pool.runTasks(&work, visit, work.count);
        for (unsigned j = 0; j < 120; ++j)
            require(work.visits[j].load() == unsigned(j < work.count));
        const auto cap = cpphdl::graph_runtime::nativeTaskWorkers(capacity);
        const auto limit = i % 2 ? std::min(cap, 1 + i % 30) : cap;
        const std::set<std::thread::id> workers(work.workers.begin(), work.workers.begin() + work.count);
        require(workers.size() <= limit);
    }
}

static void dependencies(Threads& pool, unsigned count) {
    struct Work { std::array<unsigned, 30> values{}; } work;
    // Every ancestor must wake its children before entering a task which may
    // wait for them. A chain from the last lane back to the caller exercises it.
    struct Context { Work* work; unsigned count; } ctx{&work, count};
    for (unsigned i = 0; i < 128; ++i) {
        pool.run(&ctx, [](void* ptr, Threads& executor, unsigned lane, uint32_t epoch) noexcept {
            auto& c = *static_cast<Context*>(ptr);
            if (lane + 1 < c.count) executor.wait(lane + 1, epoch);
            c.work->values[lane] = lane + 1 == c.count ? 1 : c.work->values[lane + 1] + 1;
            executor.complete(lane, epoch);
        }, count);
        for (unsigned j = 0; j < count; ++j) require(work.values[j] == count - j);
    }
    // Multiple readers may wait on one task's completion.
    pool.run(&ctx, [](void* ptr, Threads& executor, unsigned lane, uint32_t epoch) noexcept {
        auto& c = *static_cast<Context*>(ptr);
        if (!lane) { c.work->values[0] = 71; executor.complete(0, epoch); }
        else { executor.wait(0, epoch); c.work->values[lane] = c.work->values[0]; }
    }, count);
    for (unsigned j = 0; j < count; ++j) require(work.values[j] == 71);
}

static void exceptionDrain(Threads& pool, unsigned count) {
    struct Work {
        std::atomic<unsigned> started{0}, done{0};
        std::atomic<bool> release{false};
    } work;
    bool caught = false;
    try {
        pool.run(&work, [](void* ptr, Threads&, unsigned lane, uint32_t) noexcept {
            if (!lane) return;
            auto& w = *static_cast<Work*>(ptr);
            w.started.fetch_add(1);
            while (!w.release.load()) std::this_thread::yield();
            w.done.fetch_add(1);
        }, count, [&] {
            while (work.started.load() != count - 1) std::this_thread::yield();
            require(work.done.load() == 0);
            work.release.store(true);
            throw std::runtime_error("continuation");
        });
    } catch (const std::runtime_error&) { caught = true; }
    require(caught && work.done.load() == count - 1);
}

int main(int argc, char**) {
    if (argc > 1) {
        try { Threads pool(2, 2); }
        catch (const std::invalid_argument&) { return 0; }
        return 1;
    }
    const char* memorySetting=std::getenv("CPPHDL_NATIVE_MEMORY_WORKERS");
    const bool hadMemorySetting=memorySetting;
    const std::string savedMemorySetting=memorySetting?memorySetting:"";
    unsetenv("CPPHDL_NATIVE_MEMORY_WORKERS");
    require(cpphdl::graph_runtime::nativeMemoryWorkers(6)==6);
    setenv("CPPHDL_NATIVE_MEMORY_WORKERS","2",1);
    require(cpphdl::graph_runtime::nativeMemoryWorkers(6)==2);
    require(cpphdl::graph_runtime::nativeMemoryWorkers(1)==1);
    for(const char* invalid:{"0","-1","257","","oops"}) {
        setenv("CPPHDL_NATIVE_MEMORY_WORKERS",invalid,1);
        bool rejected=false;
        try { (void)cpphdl::graph_runtime::nativeMemoryWorkers(6); }
        catch(const std::invalid_argument&) { rejected=true; }
        require(rejected);
    }
    if(hadMemorySetting)setenv("CPPHDL_NATIVE_MEMORY_WORKERS",savedMemorySetting.c_str(),1);
    else unsetenv("CPPHDL_NATIVE_MEMORY_WORKERS");
    for (unsigned count : {1u, 2u, 6u, 30u}) {
        Threads pool(count, 120);
        bool invalidBudget = false;
        try { pool.runTasks(nullptr, [](void*, Threads&, unsigned, uint32_t) noexcept {}, 1, 0); }
        catch (const std::invalid_argument&) { invalidBudget = true; }
        require(invalidBudget);
        subsets(pool, count);
        tasks(pool, count);
        dependencies(pool, count);
        exceptionDrain(pool, count);
        // Concurrent model copies share one executor, but never job metadata.
        std::thread a([&] { subsets(pool, count); });
        std::thread b([&] { tasks(pool, count); });
        a.join(); b.join();
        for (unsigned invalid : {0u, count + 1}) {
            bool caught = false;
            try { pool.run(nullptr, [](void*, Threads&, unsigned, uint32_t) noexcept {}, invalid); }
            catch (const std::invalid_argument&) { caught = true; }
            require(caught);
        }
    }
    for (unsigned i = 0; i < 32; ++i) {
        Threads pool(30, 30);
        pool.run(nullptr, [](void*, Threads&, unsigned, uint32_t) noexcept {}, 1u);
    }
    std::puts("PASS native runtime: subsets, task queue, dependencies, exceptions, shared executor, shutdown (1/2/6/30 workers)");
}
