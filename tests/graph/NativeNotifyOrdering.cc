// Standalone memory-ordering diagnostic, not a reproduction of a whole graph
// hang. Models the waiter-count check in older libstdc++ atomic::notify.
// Run: c++ -std=c++20 -O2 -pthread NativeNotifyOrdering.cc -o notify-ordering
//      ./notify-ordering [iterations]
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#if defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>
#endif

struct alignas(64) Word { std::atomic<unsigned> value{0}; };

static unsigned exercise(unsigned iterations, std::memory_order publication) {
    Word stamp, waiters;
    unsigned sawWaiter = 0, sawStamp = 0, missed = 0;
    std::barrier rendezvous(2);
    std::thread waiter([&] {
        for (unsigned i = 0; i < iterations; ++i) {
            rendezvous.arrive_and_wait();
            waiters.value.fetch_add(1, std::memory_order_seq_cst);
            sawStamp = stamp.value.load(std::memory_order_acquire);
            rendezvous.arrive_and_wait();
        }
    });
    for (unsigned i = 0; i < iterations; ++i) {
        stamp.value.store(0);
        waiters.value.store(0);
#if defined(__i386__) || defined(__x86_64__)
        // Widen the store-buffer window without exhausting host RAM.
        _mm_clflush(&stamp);
        _mm_mfence();
#endif
        rendezvous.arrive_and_wait();
        // Keep the two publications separate so the compiler doesn't promote
        // a runtime memory_order argument to seq_cst in both cases.
        if (publication == std::memory_order_seq_cst)
            stamp.value.store(1, std::memory_order_seq_cst);
        else
            stamp.value.store(1, std::memory_order_release);
        sawWaiter = waiters.value.load(std::memory_order_seq_cst);
        rendezvous.arrive_and_wait();
        // The notifier would skip waking, while the waiter would proceed
        // toward sleeping. A later kernel value check may still prevent sleep;
        // this count therefore is NOT a count of actual lost kernel wakeups.
        if (!sawWaiter && !sawStamp) ++missed;
    }
    waiter.join();
    return missed;
}

int main(int argc, char** argv) {
    const unsigned iterations = argc > 1 ? std::stoul(argv[1]) : 1000000;
    const unsigned release = exercise(iterations, std::memory_order_release);
    const unsigned ordered = exercise(iterations, std::memory_order_seq_cst);
    std::printf("iterations=%u release_both_stale=%u seq_cst_both_stale=%u\n",
                iterations, release, ordered);
    // The baseline is timing dependent and may be zero. The ordered variant
    // must forbid the outcome on every architecture.
    return ordered ? EXIT_FAILURE : EXIT_SUCCESS;
}
