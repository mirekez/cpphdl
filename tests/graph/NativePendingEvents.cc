#include "cpphdl_graph_events.h"
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

using cpphdl::graph_runtime::PendingEvents;
void require(bool ok) { if (!ok) throw std::runtime_error("pending event mismatch"); }
template<std::size_t Words> void check() {
    PendingEvents<Words> set, merged;
    if constexpr (Words) { set.addWord(Words - 1, 0); require(set.size() == 0); }
    std::array<uint64_t, Words> expected{};
    for (unsigned round = 0; round < 64; ++round) {
        for (std::size_t i = 0; i < Words * 64; ++i) {
            if ((i + round) % 7 && round != 0) continue;
            set.set(i); set.set(i); expected[i / 64] |= uint64_t(1) << (i % 64);
        }
        auto copy = set; // Copy pending state before publication.
        merged.mergeFrom(set); require(set.size() == 0);
        merged.mergeFrom(copy); require(copy.size() == 0);
        merged.mergeFrom(merged);
        std::array<bool, Words> visited{};
        merged.drain([&](std::size_t word, uint64_t bits) noexcept {
            if (visited[word] || bits != expected[word]) std::terminate();
            visited[word] = true;
        });
        require(merged.size() == 0);
        for (std::size_t i = 0; i < Words; ++i) require(visited[i] == bool(expected[i]));
        expected.fill(0);
        merged.drain([](auto, auto) noexcept { std::terminate(); });
    }
}
void parallelCheck() {
    constexpr unsigned count = 30, words = 65;
    auto sets = std::make_unique<std::array<PendingEvents<words>, count>>();
    std::vector<std::thread> writers;
    for (unsigned lane = 0; lane < count; ++lane) writers.emplace_back([&, lane] {
        for (unsigned i = 0; i < 10000; ++i) (*sets)[lane].set((i * 17 + lane * 31) % (words * 64));
    });
    for (auto& writer : writers) writer.join();
    PendingEvents<words> merged;
    for (auto& set : *sets) merged.mergeFrom(set);
    unsigned visited = 0;
    merged.drain([&](auto, auto bits) noexcept { require(bits == ~uint64_t(0)); ++visited; });
    require(visited == words);
}

// Match generated publication: one bitmap per task, OR/clear every task's
// word on the caller, then mark each dirty chunk once. The replacement merges
// touched words and preserves the same deduplication before marking chunks.
constexpr std::size_t Words = 4096, Tasks = 120;
struct alignas(64) Dirty { bool pending[2]{}; };
struct alignas(64) DenseSet { std::array<uint64_t, Words> words{}; void set(std::size_t bit) { words[bit/64] |= uint64_t(1) << (bit%64); } };
template<class Set> struct Fixture {
    std::array<Set, Tasks> sets{};
    PendingEvents<Words> merged;
    std::array<Dirty, Words * 64> dirty{};
    void publishWord(std::size_t word, uint64_t bits) {
        while (bits) {
            auto& chunk = dirty[word * 64 + std::countr_zero(bits)];
            chunk.pending[0] = chunk.pending[1] = true;
            bits &= bits - 1;
        }
    }
    template<std::size_t... I> void publish(std::index_sequence<I...>) {
        if constexpr (std::is_same_v<Set, DenseSet>) {
            for (std::size_t word = 0; word < Words; ++word) {
                const uint64_t bits = (uint64_t(0) | ... | sets[I].words[word]);
                ((sets[I].words[word] = 0), ...);
                publishWord(word, bits);
            }
        } else {
            (merged.mergeFrom(sets[I]), ...);
            merged.drain([&](auto word, auto bits) noexcept { publishWord(word, bits); });
        }
    }
};
template<class Set> double bench(unsigned active, unsigned changed, unsigned rounds, uint64_t& checksum) {
    auto f = std::make_unique<Fixture<Set>>();
    const auto start = std::chrono::steady_clock::now();
    for (unsigned round = 0; round < rounds; ++round) {
        for (unsigned lane = 0; lane < active; ++lane)
            for (unsigned i = 0; i < changed; ++i)
                f->sets[lane].set(((i * 17 + lane * 31 + round) % Words) * 64 + round % 64);
        f->publish(std::make_index_sequence<Tasks>{});
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    checksum = 0;
    for (const auto& d : f->dirty) checksum = checksum * 131 + d.pending[0] + 2 * d.pending[1];
    return elapsed;
}
int main(int argc, char** argv) {
    check<0>(); check<1>(); check<2>(); check<65>(); parallelCheck();
    std::puts("PASS pending events: boundaries, duplicates, dense/sparse, drain/reuse, copies, 30 writers");
    if (argc < 2 || std::string_view(argv[1]) != "--benchmark") return 0;
    std::puts("trial\tcase\tdense_seconds\tsparse_seconds\tspeedup\tchecksum");
    for (unsigned trial = 0; trial < 3; ++trial) {
        for (unsigned kind = 0; kind < 4; ++kind) {
            const unsigned active = kind == 0 ? 0 : kind == 1 ? 8 : Tasks;
            const unsigned changed = kind == 3 ? Words : 8;
            const unsigned rounds = kind == 3 ? 128 : 2048;
            uint64_t oldSum = 0, newSum = 0;
            double oldTime, newTime;
            if (trial % 2) {
                newTime = bench<PendingEvents<Words>>(active, changed, rounds, newSum);
                oldTime = bench<DenseSet>(active, changed, rounds, oldSum);
            } else {
                oldTime = bench<DenseSet>(active, changed, rounds, oldSum);
                newTime = bench<PendingEvents<Words>>(active, changed, rounds, newSum);
            }
            require(oldSum == newSum);
            const char* names[] = {"idle", "8-active", "120-active", "dense"};
            std::printf("%u\t%s\t%.6f\t%.6f\t%.2f\t%016llx\n", trial, names[kind], oldTime, newTime, oldTime/newTime, (unsigned long long)oldSum);
            std::fflush(stdout);
        }
    }
}
