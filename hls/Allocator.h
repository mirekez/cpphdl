#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

// This boundary becomes a latched hardware fault in the scheduled backend.
extern "C" [[noreturn]] void cpphdl_hls_fault(uint32_t code);

namespace cpphdl::hls {

template<size_t Bytes>
class Arena {
    static constexpr size_t Block = 16;
    static_assert(Bytes > 0 && Bytes % Block == 0);
    alignas(Block) unsigned char data[Bytes];
    unsigned char used[Bytes / Block];
public:
    Arena() : used{} {}
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* allocate(size_t bytes) {
        if (bytes == 0 || bytes > Bytes) cpphdl_hls_fault(1);
        size_t needed = (bytes + Block - 1) / Block;
        size_t run = 0;
        for (size_t i = 0; i < Bytes / Block; ++i) {
            run = used[i] ? 0 : run + 1;
            if (run == needed) {
                size_t first = i + 1 - needed;
                for (size_t j = first; j <= i; ++j) used[j] = 1;
                return data + first * Block;
            }
        }
        cpphdl_hls_fault(1);
    }

    void deallocate(void* ptr, size_t bytes) {
        uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t base = reinterpret_cast<uintptr_t>(data);
        if (address < base || address - base >= Bytes ||
            (address - base) % Block || bytes == 0 || bytes > Bytes - (address - base))
            cpphdl_hls_fault(2);
        size_t first = (address - base) / Block;
        size_t count = (bytes + Block - 1) / Block;
        for (size_t i = first; i < first + count; ++i) {
            if (!used[i]) cpphdl_hls_fault(2);
            used[i] = 0;
        }
    }
};

template<class T, size_t Bytes>
class Allocator {
    template<class, size_t> friend class Allocator;
    Arena<Bytes>* arena;
public:
    using value_type = T;
    using is_always_equal = std::false_type;
    template<class U> struct rebind { using other = Allocator<U, Bytes>; };

    explicit Allocator(Arena<Bytes>& storage) noexcept : arena(&storage) {}
    template<class U>
    Allocator(const Allocator<U, Bytes>& other) noexcept : arena(other.arena) {}

    T* allocate(size_t count) {
        static_assert(alignof(T) <= 16, "Over-aligned allocation is unsupported");
        if (count > Bytes / sizeof(T)) cpphdl_hls_fault(1);
        return static_cast<T*>(arena->allocate(count * sizeof(T)));
    }
    void deallocate(T* ptr, size_t count) noexcept {
        arena->deallocate(ptr, count * sizeof(T));
    }
    size_t max_size() const noexcept { return Bytes / sizeof(T); }
    template<class U> bool operator==(const Allocator<U, Bytes>& other) const noexcept {
        return arena == other.arena;
    }
    template<class U> bool operator!=(const Allocator<U, Bytes>& other) const noexcept {
        return !(*this == other);
    }
};
}
