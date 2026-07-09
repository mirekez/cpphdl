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
    return (N + ...);
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
logic<WIDTH> cat_to_logic(const logic<WIDTH>& value)
{
    return value;
}

template<size_t WIDTH>
logic<WIDTH> cat_to_logic(const u<WIDTH>& value)
{
    logic<WIDTH> result{};
    std::memset(result.bytes, 0, sizeof(result.bytes));
    uint64_t raw = value;
    for (size_t i = 0; i < WIDTH; ++i) {
        result.set(i, (raw >> i) & 1);
    }
    return result;
}

inline logic<8> cat_to_logic(const u1& value) { return cat_to_logic(u<8>((uint64_t)value)); }
inline logic<8> cat_to_logic(const u8& value) { return cat_to_logic(u<8>((uint64_t)value)); }
inline logic<16> cat_to_logic(const u16& value) { return cat_to_logic(u<16>((uint64_t)value)); }
inline logic<32> cat_to_logic(const u32& value) { return cat_to_logic(u<32>((uint64_t)value)); }
inline logic<64> cat_to_logic(const u64& value) { return cat_to_logic(u<64>((uint64_t)value)); }

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

    void append(size_t& high_offset)
    {
        (void)high_offset;
    }

    template<size_t ARG_WIDTH, typename... Args>
    void append(size_t& high_offset, const logic<ARG_WIDTH>& arg, const Args&... args)
    {
        high_offset -= ARG_WIDTH;
        for (size_t i = 0; i < ARG_WIDTH; ++i) {
            (*static_cast<logic<WIDTH>*>(this)).set(high_offset + i, arg.get(i));
        }
        append(high_offset, args...);
    }

    template<typename... Args>
    cat(const Args&... args)
    {
        static_assert(sizeof...(Args) == sizeof...(N), "cat argument count mismatch");
        static_assert(((cat_width_v<Args> == N) && ...), "cat argument width mismatch");
        std::memset(this->bytes, 0, sizeof(this->bytes));
        size_t high_offset = WIDTH;
        append(high_offset, cat_to_logic(args)...);
    }

    const cat& operator=(const cat& other)
    {
        std::memcpy(this->bytes, other.bytes, std::min(sizeof(this->bytes), sizeof(other.bytes)));
        return *this;
    }

    operator uint64_t() const
    {
        return this->to_ullong();
    }
};

template<typename... Args>
cat(const Args&...) -> cat<cat_width_v<Args>...>;

template<size_t... N>
using Cat = cat<N...>;

}
