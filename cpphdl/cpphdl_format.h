#pragma once

#include "cpphdl_logic.h"
#include "cpphdl_array.h"

#ifdef USE_FORMAT_H
//////////////////////////////////////// just to print pretty

template<typename T>
concept OnlyCppHdlClass = is_from_cpphdl_namespace<T>::value;

template<OnlyCppHdlClass T, size_t N>
struct std::formatter<T[N]>: public std::formatter<T>
{
    template <typename FormatContext>
    auto format(const T (&arr)[N], FormatContext& ctx) const
    {
        auto out = ctx.out();
        for (size_t i = 0; i < N; ++i) {
            out = std::formatter<T>::format(static_cast<T>(arr[N-1-i]), ctx);
        }
        return out;
    }
};

template<typename T>
struct std::formatter<cpphdl::reg<T>>: public std::formatter<T>
{
    template <typename FormatContext>
    auto format(const T& val, FormatContext& ctx) const {
        return std::formatter<T>::format(val, ctx);
    }
};

template<typename T, size_t N>
struct std::formatter<cpphdl::array<T,N>>: public std::formatter<T>
{
    template <typename FormatContext>
    auto format(const cpphdl::array<T,N> &arr, FormatContext& ctx) const {
        auto out = ctx.out();
        *out++ = '[';
        for (size_t i = 0; i < N; ++i) {
            if (i > 0) *out++ = ' ';
            out = std::formatter<T>::format(static_cast<T>(arr[N-1-i]), ctx);
        }
        *out++ = ']';
        return out;
    }
};

template<size_t N>
struct std::formatter<cpphdl::array<cpphdl::u8,N>>: public std::formatter<cpphdl::u8>
{
    template <typename FormatContext>
    auto format(const cpphdl::array<cpphdl::u8,N> &arr, FormatContext& ctx) const {
        auto out = ctx.out();
        *out++ = '[';
        for (size_t i = 0; i < N; ++i) {
//            if (i > 0) *out++ = ' ';  // dont like spaces for byte arrays
            out = std::formatter<cpphdl::u8>::format(static_cast<cpphdl::u8>(arr[N-1-i]), ctx);
        }
        *out++ = ']';
        return out;
    }
};

// logic
template<size_t SIZE>
struct std::formatter<cpphdl::logic<SIZE>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    template <typename FormatContext>
    auto format(const cpphdl::logic<SIZE>& logic, FormatContext& ctx) const {
        auto out = ctx.out();
        out = std::format_to(out, "{}", logic.to_hex());
        return out;
    }
};

#endif
