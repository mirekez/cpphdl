#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "cpphdl_logic.h"
#include "cpphdl_types.h"

namespace cpphdl
{

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

template<typename T>
constexpr size_t cat_width_v = cat_width<std::remove_cvref_t<T>>::value;

template<size_t WIDTH>
logic<WIDTH> cat_to_logic(const logic<WIDTH>& value)
{
    return value;
}

template<size_t WIDTH>
logic<WIDTH> cat_to_logic(const u<WIDTH>& value)
{
    logic<WIDTH> result{};
    uint64_t raw = value;
    for (size_t i = 0; i < WIDTH; ++i) {
        result.set(i, (raw >> i) & 1);
    }
    return result;
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
