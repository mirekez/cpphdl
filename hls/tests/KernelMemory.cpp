#include <cstdint>
#include <cstring>
#include <new>
#include "../Allocator.h"

struct MemoryKernel {
    unsigned char bytes[32];
    unsigned char* cursor;
    cpphdl::hls::Arena<64> arena;
    uint64_t* allocation;
    MemoryKernel() : bytes{}, cursor(bytes), allocation(nullptr) {}
    uint64_t command(uint32_t op, uint32_t index, uint32_t value) {
        uint32_t word = 0;
        switch (op) {
        case 0: std::memcpy(bytes + index % 29, &value, 4); return value;
        case 1: std::memcpy(&word, bytes + index % 29, 4); return word;
        case 2: std::memmove(bytes + index % 8 + 1, bytes, 16); return 0;
        case 3: std::memmove(bytes, bytes + index % 8 + 1, 16); return 0;
        case 4:
            for (unsigned n = 0; n < 7; ++n) {
                word = index; index = value; value = word + n;
            }
            return (uint64_t(index) << 32) | value;
        case 5: return int64_t(int8_t(value)) >> (index % 8);
        case 6: cursor = bytes + index % 32; *cursor = value; return 0;
        case 7: return *cursor;
        case 8: return *reinterpret_cast<uint32_t*>(uintptr_t(index));
        case 9: {
            cpphdl::hls::Allocator<uint32_t, 64> words(arena);
            cpphdl::hls::Allocator<uint64_t, 64> rebound(words);
            allocation = rebound.allocate(2);
            new (allocation) uint64_t((uint64_t(index) << 32) | value);
            new (allocation + 1) uint64_t(~allocation[0]);
            return allocation[0];
        }
        case 10: return allocation[index & 1];
        case 11: arena.deallocate(allocation, 16); return 0;
        default: return 0;
        }
    }
};

extern "C" const uint64_t cpphdl_hls_state_bytes = sizeof(MemoryKernel);
extern "C" const uint64_t cpphdl_hls_state_align = alignof(MemoryKernel);
extern "C" uint64_t memory_kernel(MemoryKernel* self, uint32_t op, uint32_t index, uint32_t value) {
    if (op == UINT32_MAX) { new (self) MemoryKernel; return 0; }
    return self->command(op, index, value);
}

#ifndef CPPHDL_HLS_COMPILING
#include "ScheduledBench.h"
int main() {
    MemoryKernel native;
#ifdef VERILATOR
    Bench bench; bench._assign(); bench.reset();
#endif
    auto run = [&](uint32_t op, uint32_t index, uint32_t value) {
        uint64_t expected = memory_kernel(&native, op, index, value);
#ifdef VERILATOR
        bench.transaction(op, index, value, expected, 0);
#endif
        return expected;
    };
    for (unsigned n = 0; n < 29; ++n) run(0, n, 0xfe1298ab ^ n);
    for (unsigned shift = 0; shift < 8; ++shift) {
        for (unsigned n = 0; n < 29; ++n) run(1, n, 0);
        run(2, shift, 0);
        for (unsigned n = 0; n < 29; ++n) run(1, n, 0);
        run(3, shift, 0);
    }
    for (unsigned n = 0; n < 32; ++n) {
        run(6, n, 0xe0 + n); require(run(7, 0, 0) == 0xe0 + n, "pointer identity lost");
        run(4, n + 4, 0xab39 + n); run(5, n, n + 128);
        run(9, n, 0xfeed0193); run(10, 0, 0); run(10, 1, 0); run(11, 0, 0);
    }
    bool double_free = false;
    try { memory_kernel(&native, 11, 0, 0); }
    catch (const AllocationFault& fault) { double_free = fault.code == 2; }
    require(double_free, "allocator did not detect double free");
#ifdef VERILATOR
    bench.transaction(11, 0, 0, 0, 2); bench.reset();
    for (uint32_t address : {0u, 4u, 0xfffffffcu}) {
        bench.transaction(8, address, 0, 0, 3); bench.reset();
    }
#endif
    std::puts("PASS scheduled memory: unaligned fields, overlap both directions, saved pointer, PHIs, signed values, bounds, allocator rebind/reuse/double-free");
}
#endif
