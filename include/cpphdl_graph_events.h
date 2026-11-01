#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace cpphdl::graph_runtime {

// One writer per set. Merge/drain only after the writers have joined. Record
// each nonzero bitmap word once, so sparse publication does not scan or clear
// the entire event address space. Fixed storage preserves model copy semantics
// and performs no allocations during simulation.
template<std::size_t Words>
class PendingEvents {
    static_assert(Words <= std::numeric_limits<uint32_t>::max());
public:
    void addWord(std::size_t word, uint64_t bits) noexcept {
        if (!bits) return;
        if (!words_[word]) touched_[size_++] = uint32_t(word);
        words_[word] |= bits;
    }
    void set(std::size_t bit) noexcept {
        addWord(bit / 64, uint64_t(1) << (bit % 64));
    }
    std::size_t size() const noexcept { return size_; }
    // Visitors must not throw or mutate this set while draining it.
    template<class Visitor>
    void drain(Visitor&& visit) noexcept {
        for (std::size_t i = 0; i < size_; ++i) {
            const auto word = touched_[i];
            visit(word, std::exchange(words_[word], 0));
        }
        size_ = 0;
    }
    void mergeFrom(PendingEvents& other) noexcept {
        if (this == &other) return;
        other.drain([&](std::size_t word, uint64_t bits) noexcept { addWord(word, bits); });
    }
private:
    std::array<uint64_t, Words> words_{};
    std::array<uint32_t, Words> touched_{};
    std::size_t size_ = 0;
};
}
