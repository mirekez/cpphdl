#pragma once
#include "cpphdl.h"
#include <new>
#include <type_traits>

namespace cpphdl::hls {

// The native wrapper is a transaction reference; the AST pass schedules the
// actual object methods for RTL. T is not itself required to be an RTL module.
template<class T, unsigned MAX_RECURSION = 0, unsigned ADDRESS_BITS = 16, unsigned HEAP_BYTES = 4096,
         bool SHARED_MEMORY = false, bool BLOCK_RAM = false>
class
#ifdef __clang__
[[clang::annotate("CPPHDL_HLS_CLOCKED")]]
#endif
ClockedDelayer : public cpphdl::Module {
    static_assert(ADDRESS_BITS >= 8 && ADDRESS_BITS <= 64, "ADDRESS_BITS must be in 8..64");
    static_assert(!BLOCK_RAM || SHARED_MEMORY, "BLOCK_RAM requires SHARED_MEMORY");
    static_assert(HEAP_BYTES >= 16 && HEAP_BYTES <= 16777216 && HEAP_BYTES % 16 == 0,
                  "HEAP_BYTES must be a multiple of 16 in 16..16777216");
public:
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
private:
    T object{};
    cpphdl::reg<cpphdl::u1> pending_reg;
    cpphdl::reg<cpphdl::u64> result_reg;
public:
    void _assign() {
        command_ready_out = _ASSIGN(!pending_reg);
        response_valid_out = _ASSIGN((bool)pending_reg);
        result_out = _ASSIGN((uint64_t)result_reg);
        fault_out = _ASSIGN(0u);
    }
    void _work(bool reset) {
        if (reset) {
            object.~T();
            ::new (static_cast<void*>(&object)) T{};
            pending_reg.clr(); result_reg.clr();
        } else if (pending_reg) {
            if (response_ready_in()) pending_reg._next = false;
        } else if (command_valid_in()) {
            result_reg._next = object.command(operation_in(), index_in(), value_in());
            pending_reg._next = true;
        }
    }
    void _strobe() { pending_reg.strobe(); result_reg.strobe(); }
};

// Compatibility spelling for existing designs; new code should name its policy.
template<class T, unsigned MAX_RECURSION = 0, unsigned ADDRESS_BITS = 16, unsigned HEAP_BYTES = 4096,
         bool SHARED_MEMORY = false, bool BLOCK_RAM = false>
using Clocked = ClockedDelayer<T, MAX_RECURSION, ADDRESS_BITS, HEAP_BYTES, SHARED_MEMORY, BLOCK_RAM>;

// II=1 with floating feedback. Calls sample committed state on admission and
// commit their state snapshot when the result enters the final pipeline stage.
template<class T, unsigned STAGES = 2>
class
#ifdef __clang__
[[clang::annotate("CPPHDL_HLS_PIPELINE")]]
#endif
ClockedPipeline : public cpphdl::Module {
    static_assert(STAGES >= 1 && STAGES <= 64, "ClockedPipeline STAGES must be in 1..64");
    static_assert(std::is_trivially_copyable<T>::value, "ClockedPipeline requires copyable value state");
public:
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
private:
    T object{};
    T next_object{};
    T snapshots[STAGES]{}, next_snapshots[STAGES]{};
    reg<u1> valid[STAGES];
    reg<u64> result[STAGES];
public:
    void _assign() {
        command_ready_out = _ASSIGN(!valid[STAGES-1] || response_ready_in());
        response_valid_out = _ASSIGN((bool)valid[STAGES-1]);
        result_out = _ASSIGN((uint64_t)result[STAGES-1]);
        fault_out = _ASSIGN(0u);
    }
    void _work(bool reset) {
        next_object = object;
        for (unsigned i = 0; i < STAGES; ++i) next_snapshots[i] = snapshots[i];
        if (reset) {
            next_object = T{};
            for (unsigned i = 0; i < STAGES; ++i) { valid[i].clr(); result[i].clr(); }
        } else if (command_ready_out()) {
            for (unsigned i = 1; i < STAGES; ++i) {
                valid[i]._next = valid[i-1];
                result[i]._next = result[i-1];
                next_snapshots[i] = snapshots[i-1];
            }
            valid[0]._next = command_valid_in();
            if (command_valid_in()) {
                T candidate = object;
                result[0]._next = candidate.command(operation_in(),index_in(),value_in());
                next_snapshots[0] = candidate;
                if constexpr (STAGES == 1) next_object = candidate;
            }
            if constexpr (STAGES > 1)
                if (valid[STAGES-2]) next_object = snapshots[STAGES-2];
        }
    }
    void _strobe() {
        object = next_object;
        for (unsigned i = 0; i < STAGES; ++i) snapshots[i] = next_snapshots[i];
        for (unsigned i = 0; i < STAGES; ++i) { valid[i].strobe(); result[i].strobe(); }
    }
};
}
