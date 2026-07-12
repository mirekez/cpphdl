#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "cpphdl_logic.h"
#include "cpphdl_types.h"

namespace cpphdl
{

template<typename T>
struct reg;

template<size_t... N>
constexpr size_t SUM()
{
    // A zero-argument concatenation instantiated an invalid empty fold expression.
    // Generic concatenation machinery may form this case while evaluating templates.
    // Return the SystemVerilog-equivalent zero width before folding nonempty packs.
    if constexpr (sizeof...(N) == 0) {
        return 0;
    }
    else {
        return (N + ...);
    }
}

template<typename T>
struct cat_width;

template<size_t WIDTH>
struct cat_width<logic<WIDTH>> {
    static constexpr size_t value = WIDTH;
};

template<size_t WIDTH>
struct cat_width<u<WIDTH>> {
    static constexpr size_t value = WIDTH;
};

template<> struct cat_width<u1> { static constexpr size_t value = 8; };
template<> struct cat_width<u8> { static constexpr size_t value = 8; };
template<> struct cat_width<u16> { static constexpr size_t value = 16; };
template<> struct cat_width<u32> { static constexpr size_t value = 32; };
template<> struct cat_width<u64> { static constexpr size_t value = 64; };

template<typename TYPE, size_t COUNT, bool PACKED>
struct cat_width<array<COUNT, TYPE, PACKED>> {
    static constexpr size_t value = array<COUNT, TYPE, PACKED>::_size_bits();
};

template<typename T>
struct cat_width<reg<T>> {
    static constexpr size_t value = cat_width<T>::value;
};

template<typename T>
constexpr size_t cat_width_v = cat_width<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template<size_t WIDTH>
// Concatenations are used in constant expressions for widths and parameters.
// Non-constexpr conversion helpers prevented otherwise constant cat expressions.
// Keep conversion bit-accurate while making logic and integer paths constexpr.
constexpr logic<WIDTH> cat_to_logic(const logic<WIDTH>& value)
{
    return value;
}

template<size_t WIDTH>
constexpr logic<WIDTH> cat_to_logic(const u<WIDTH>& value)
{
    logic<WIDTH> result{};
    uint64_t raw = value;
    for (size_t i = 0; i < WIDTH; ++i) {
        result.set(i, (raw >> i) & 1);
    }
    return result;
}

constexpr logic<8> cat_to_logic(const u1& value) { return cat_to_logic(u<8>((uint64_t)value)); }
constexpr logic<8> cat_to_logic(const u8& value) { return cat_to_logic(u<8>((uint64_t)value)); }
constexpr logic<16> cat_to_logic(const u16& value) { return cat_to_logic(u<16>((uint64_t)value)); }
constexpr logic<32> cat_to_logic(const u32& value) { return cat_to_logic(u<32>((uint64_t)value)); }
constexpr logic<64> cat_to_logic(const u64& value) { return cat_to_logic(u<64>((uint64_t)value)); }

template<typename TYPE, size_t COUNT, bool PACKED>
logic<array<COUNT, TYPE, PACKED>::_size_bits()> cat_to_logic(const array<COUNT, TYPE, PACKED>& value)
{
    return (logic<array<COUNT, TYPE, PACKED>::_size_bits()>)value;
}

template<typename T>
auto cat_to_logic(const reg<T>& value)
{
    return cat_to_logic(static_cast<const T&>(value));
}

template<size_t... N>
struct cat : logic<SUM<N...>()>
{
    static constexpr size_t WIDTH = SUM<N...>();

    // cat construction previously depended on memset, memcpy, and runtime conversion.
    // Those operations prevented valid SystemVerilog constant concatenations in C++.
    // Use constexpr loops and logic's constexpr scalar conversion throughout the type.
    constexpr void append(size_t& high_offset)
    {
        (void)high_offset;
    }

    template<size_t ARG_WIDTH, typename... Args>
    constexpr void append(size_t& high_offset, const logic<ARG_WIDTH>& arg, const Args&... args)
    {
        high_offset -= ARG_WIDTH;
        for (size_t i = 0; i < ARG_WIDTH; ++i) {
            (*static_cast<logic<WIDTH>*>(this)).set(high_offset + i, arg.get(i));
        }
        append(high_offset, args...);
    }

    template<typename... Args>
    constexpr cat(const Args&... args) : logic<WIDTH>(0)
    {
        static_assert(sizeof...(Args) == sizeof...(N), "cat argument count mismatch");
        static_assert(((cat_width_v<Args> == N) && ...), "cat argument width mismatch");
        size_t high_offset = WIDTH;
        append(high_offset, cat_to_logic(args)...);
    }

    constexpr const cat& operator=(const cat& other)
    {
        for (size_t i = 0; i < sizeof(this->bytes) && i < sizeof(other.bytes); ++i) {
            this->bytes[i] = other.bytes[i];
        }
        return *this;
    }

    constexpr operator uint64_t() const
    {
        return static_cast<const logic<WIDTH>*>(this)->to_uint64_constexpr();
    }

    template<typename T, typename std::enable_if_t<std::is_integral_v<T>, int> = 0>
    constexpr explicit operator T() const
    {
        return static_cast<T>(static_cast<const logic<WIDTH>*>(this)->to_uint64_constexpr());
    }
};

template<typename... Args>
cat(const Args&...) -> cat<cat_width_v<Args>...>;

// Generated arithmetic compares concatenations directly with integral expressions.
// Inherited logic conversions leave several built-in candidates and cause ambiguity.
// Constrain explicit overloads to integral peers and compare the cat bit value.
template<typename T>
using enable_cat_integral_t = std::enable_if_t<
    std::is_integral_v<std::remove_cv_t<std::remove_reference_t<T>>>, int>;

template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator==(const cat<N...>& lhs, T rhs) { return static_cast<uint64_t>(lhs) == static_cast<uint64_t>(rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator==(T lhs, const cat<N...>& rhs) { return static_cast<uint64_t>(lhs) == static_cast<uint64_t>(rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator!=(const cat<N...>& lhs, T rhs) { return !(lhs == rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator!=(T lhs, const cat<N...>& rhs) { return !(lhs == rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator<(const cat<N...>& lhs, T rhs) { return static_cast<uint64_t>(lhs) < static_cast<uint64_t>(rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator<(T lhs, const cat<N...>& rhs) { return static_cast<uint64_t>(lhs) < static_cast<uint64_t>(rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator<=(const cat<N...>& lhs, T rhs) { return !(rhs < lhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator<=(T lhs, const cat<N...>& rhs) { return !(rhs < lhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator>(const cat<N...>& lhs, T rhs) { return rhs < lhs; }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator>(T lhs, const cat<N...>& rhs) { return rhs < lhs; }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator>=(const cat<N...>& lhs, T rhs) { return !(lhs < rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr bool operator>=(T lhs, const cat<N...>& rhs) { return !(lhs < rhs); }

template<size_t... N>
constexpr uint64_t operator-(const cat<N...>& value) { return -static_cast<uint64_t>(value); }

// SystemVerilog permits concatenations as operands of ordinary integral operators.
// C++ overload resolution cannot consistently choose through the logic base class.
// Define symmetric cat operations that explicitly use the concatenated uint64_t value.
#define CPPHDL_CAT_BINARY_OP(OP) \
template<typename T, size_t... N, enable_cat_integral_t<T> = 0> \
constexpr uint64_t operator OP(const cat<N...>& lhs, T rhs) { return static_cast<uint64_t>(lhs) OP static_cast<uint64_t>(rhs); } \
template<typename T, size_t... N, enable_cat_integral_t<T> = 0> \
constexpr uint64_t operator OP(T lhs, const cat<N...>& rhs) { return static_cast<uint64_t>(lhs) OP static_cast<uint64_t>(rhs); } \
template<size_t... L, size_t... R> \
constexpr uint64_t operator OP(const cat<L...>& lhs, const cat<R...>& rhs) { return static_cast<uint64_t>(lhs) OP static_cast<uint64_t>(rhs); }

CPPHDL_CAT_BINARY_OP(+)
CPPHDL_CAT_BINARY_OP(-)
CPPHDL_CAT_BINARY_OP(*)
CPPHDL_CAT_BINARY_OP(/)
CPPHDL_CAT_BINARY_OP(%)
CPPHDL_CAT_BINARY_OP(&)
CPPHDL_CAT_BINARY_OP(|)
CPPHDL_CAT_BINARY_OP(^)
#undef CPPHDL_CAT_BINARY_OP

template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr uint64_t operator<<(const cat<N...>& lhs, T rhs) { return static_cast<uint64_t>(lhs) << static_cast<unsigned>(rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr uint64_t operator<<(T lhs, const cat<N...>& rhs) { return static_cast<uint64_t>(lhs) << static_cast<unsigned>(static_cast<uint64_t>(rhs)); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr uint64_t operator>>(const cat<N...>& lhs, T rhs) { return static_cast<uint64_t>(lhs) >> static_cast<unsigned>(rhs); }
template<typename T, size_t... N, enable_cat_integral_t<T> = 0>
constexpr uint64_t operator>>(T lhs, const cat<N...>& rhs) { return static_cast<uint64_t>(lhs) >> static_cast<unsigned>(static_cast<uint64_t>(rhs)); }

template<size_t... N>
using Cat = cat<N...>;

}
