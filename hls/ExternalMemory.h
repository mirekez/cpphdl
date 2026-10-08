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
    static_assert(std::is_integral<T>::value && sizeof(T) <= 8,
                  "external memory elements must be integers up to 64 bits");
    const auto& binding = external_memory_binding;
    if (!binding.data || byte_address < binding.base || sizeof(T) > binding.bytes ||
        size_t(byte_address - binding.base) > binding.bytes - sizeof(T) ||
        byte_address % alignof(T)) std::abort();
    return (external_ptr<T>)(static_cast<unsigned char*>(binding.data) + byte_address - binding.base);
}

// Controller-facing protocol: one request and one acknowledgement per access.
// Data occupies the low size bytes, independent of the byte address's lane.
template<unsigned ADDRESS_BITS = 32>
struct ExternalMemoryIf : public Interface {
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
