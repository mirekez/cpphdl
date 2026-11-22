#pragma once
#include "cpphdl.h"
#include <cstdlib>
#include <cstdint>
#include <type_traits>

namespace cpphdl::hls {

// Address space 1 distinguishes controller addresses from the local HLS arena.
// GCC uses ordinary pointers for the native transaction reference.
template<class T>
using external_ptr = T
#ifdef __clang__
    __attribute__((address_space(1)))
#endif
    *;

struct ExternalMemoryBinding {
    void* data = nullptr;
    size_t bytes = 0;
    uint32_t base = 0;
};
inline thread_local ExternalMemoryBinding external_memory_binding;

// Testbench setup only. Each simulation thread owns its native address mapping.
inline void bind_external_memory(void* data, size_t bytes, uint32_t base = 0)
{
    external_memory_binding = {data, bytes, base};
}

template<class T>
#ifdef __clang__
[[clang::annotate("CPPHDL_EXTERNAL_POINTER")]]
#endif
external_ptr<T> external_memory(uint32_t byte_address)
{
    static_assert(std::is_trivially_copyable<T>::value && std::is_standard_layout<T>::value &&
                  !std::is_pointer<T>::value && sizeof(T) <= 64 && (sizeof(T) & (sizeof(T)-1)) == 0,
                  "external memory elements must be power-of-two trivial values up to 64 bytes");
    const auto& binding = external_memory_binding;
    if (!binding.data || byte_address < binding.base || sizeof(T) > binding.bytes ||
        size_t(byte_address - binding.base) > binding.bytes - sizeof(T) ||
        byte_address % alignof(T)) std::abort();
    return (external_ptr<T>)(static_cast<unsigned char*>(binding.data) + byte_address - binding.base);
}

// Clang's implicit aggregate copy operations cannot bind address-space-qualified
// objects. These typed value transfers retain the address space through HLS;
// their native implementation has ordinary C++ object-copy semantics.
template<class T>
void check_external_value(external_ptr<T> pointer)
{
    const auto& binding = external_memory_binding;
    const auto offset = reinterpret_cast<uintptr_t>(pointer) - reinterpret_cast<uintptr_t>(binding.data);
    if (!binding.data || sizeof(T) > binding.bytes || offset > binding.bytes - sizeof(T) ||
        (offset + binding.base) % sizeof(T)) std::abort();
}

template<class T>
#ifdef __clang__
[[clang::annotate("CPPHDL_EXTERNAL_LOAD")]]
#endif
std::remove_cv_t<T> load_external(external_ptr<T> pointer)
{
    static_assert(std::is_trivially_copyable<T>::value && !std::is_volatile<T>::value);
    check_external_value(pointer);
    std::remove_cv_t<T> value;
    __builtin_memcpy(&value, pointer, sizeof(value));
    return value;
}

template<class T>
#ifdef __clang__
[[clang::annotate("CPPHDL_EXTERNAL_STORE")]]
#endif
void store_external(external_ptr<T> pointer, const T& value)
{
    static_assert(std::is_trivially_copyable<T>::value && !std::is_const<T>::value && !std::is_volatile<T>::value);
    check_external_value(pointer);
    __builtin_memcpy(pointer, &value, sizeof(value));
}

// Controller-facing protocol: one request and one acknowledgement per access.
// Data occupies the low size bytes, independent of the byte address's lane.
// Access sizes are powers of two and addresses are naturally aligned to size.
// ClockedMemory defaults to one outstanding request. Its opt-in MAX_READS=2
// mode requires ordered responses and side-effect-free reads (no MMIO): a later
// read may be issued before an earlier read succeeds. Stores remain serialized.
// ClockedMemory's STREAMING policy uses MAX_READS as its context-queue depth:
// it can issue a request each cycle and resumes the matching source calculation
// on each ordered response, independently of other pending requests.
// In write-bearing commands, potentially aliasing invocations keep their memory
// regions ordered through the last acknowledgement. Runtime-loop pipelines
// admit one invocation until return; loop backedges reuse arithmetic. Within
// an external-to-local copy loop, memory-independent iterations may issue ahead
// under the same side-effect-free read contract. Local writes remain ordered;
// a failed read suppresses later writes and the exit drains all issued reads.
// Reset both endpoints and discard pending responses before admitting new work.
template<unsigned ADDRESS_BITS = 32, unsigned DATA_BITS = 64>
struct ExternalMemoryIf : public Interface {
    static_assert(DATA_BITS >= 64 && DATA_BITS <= 512 && (DATA_BITS & (DATA_BITS-1)) == 0,
                  "external memory data width must be 64, 128, 256 or 512 bits");
    using Data = logic<DATA_BITS>;
    _PORT(bool) valid_in;
    _PORT(bool) write_in;
    _PORT(logic<ADDRESS_BITS>) addr_in;
    _PORT(uint8_t) size_in;
    _PORT(logic<DATA_BITS>) data_in;
    _PORT(bool) ready_out;
    _PORT(bool) valid_out;
    _PORT(logic<DATA_BITS>) data_out;
    _PORT(bool) error_out;
    _PORT(bool) ready_in;
};

// Keep the original 64-bit C++ port type. An explicit specialization also
// leaves the symbolic SV interface a concrete payload type; a dependent
// std::conditional_t cannot be emitted as a SystemVerilog type expression.
template<unsigned ADDRESS_BITS>
struct ExternalMemoryIf<ADDRESS_BITS,64> : public Interface {
    using Data = uint64_t;
    _PORT(bool) valid_in;
    _PORT(bool) write_in;
    _PORT(logic<ADDRESS_BITS>) addr_in;
    _PORT(uint8_t) size_in;
    _PORT(uint64_t) data_in;
    _PORT(bool) ready_out;
    _PORT(bool) valid_out;
    _PORT(uint64_t) data_out;
    _PORT(bool) error_out;
    _PORT(bool) ready_in;
};

// Read-only, ordered channel to a DDR controller. The connected modules define
// the outstanding-request limit; responses must preserve acceptance order.
// Addresses are bytes; data contains DATA_BITS/8 consecutive little-endian bytes.
// This is a controller-side channel, not DDR pins or an AXI implementation.
template<unsigned DATA_BITS = 512, unsigned ADDRESS_BITS = 32>
struct DramReadIf : public Interface {
    static_assert(DATA_BITS >= 64 && DATA_BITS <= 512 && (DATA_BITS & (DATA_BITS - 1)) == 0);
    static_assert(ADDRESS_BITS >= 8 && ADDRESS_BITS <= 32);
    _PORT(bool) valid_in;
    _PORT(logic<ADDRESS_BITS>) addr_in;
    _PORT(bool) ready_out;
    _PORT(bool) valid_out;
    _PORT(logic<DATA_BITS>) data_out;
    _PORT(bool) error_out;
    _PORT(bool) ready_in;
};

}
