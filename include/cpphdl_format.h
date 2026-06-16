#pragma once

#include <type_traits>
#include <utility>

#include "cpphdl_logic.h"
#include "cpphdl_array.h"
#include "cpphdl_memory.h"

#ifdef CPPHDL_HAS_STD_FORMAT

//////////////////////////////////////// just to print pretty

#ifndef CPPHDL_DISABLE_RAW_FIELD_FORMATTER

template<typename T, typename = void>
struct cpphdl_has_raw_field : std::false_type {};

template<typename T>
struct cpphdl_has_raw_field<T, std::void_t<decltype(std::declval<const T&>().raw)>> : std::true_type {};

template<typename T>
struct cpphdl_raw_field_formatter_enabled
{
    constexpr static bool value = (std::is_class<T>::value || std::is_union<T>::value)
        && cpphdl_has_raw_field<T>::value
        && !is_from_cpphdl_namespace<T>::value;
};

template<typename T>
struct std::formatter<T, std::enable_if_t<cpphdl_raw_field_formatter_enabled<T>::value, char>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const T& val, FormatContext& ctx) const
    {
        auto out = ctx.out();
        if constexpr (sizeof(T) <= 1) {
            out = std::format_to(out, "{:02x}", (uint64_t)val.raw);
        }
        else if constexpr (sizeof(T) <= 2) {
            out = std::format_to(out, "{:04x}", (uint64_t)val.raw);
        }
        else if constexpr (sizeof(T) <= 4) {
            out = std::format_to(out, "{:08x}", (uint64_t)val.raw);
        }
        else {
            out = std::format_to(out, "{:016x}", (uint64_t)val.raw);
        }
        return out;
    }
};

#endif

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
            if (i > 0) *out++ = ' ';
            out = std::formatter<T>::format(static_cast<T>(arr[i]), ctx);
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

template<typename T, size_t N, bool PACKED>
struct std::formatter<cpphdl::array<T,N,PACKED>>: public std::formatter<T>
{
    template <typename FormatContext>
    auto format(const cpphdl::array<T,N,PACKED> &arr, FormatContext& ctx) const {
        auto out = ctx.out();
        *out++ = '[';
        for (size_t i = 0; i < N; ++i) {
            if (i > 0) *out++ = ' ';
            out = std::formatter<T>::format(static_cast<T>(arr[i]), ctx);
        }
        *out++ = ']';
        return out;
    }
};

// printing u8 arrays from right to left and without spaces

template<size_t N>
struct std::formatter<cpphdl::u8[N]>: public std::formatter<cpphdl::u8>
{
    template <typename FormatContext>
    auto format(const cpphdl::u8 (&arr)[N], FormatContext& ctx) const {
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

template<size_t N, bool PACKED>
struct std::formatter<cpphdl::array<cpphdl::u8,N,PACKED>>: public std::formatter<cpphdl::u8>
{
    template <typename FormatContext>
    auto format(const cpphdl::array<cpphdl::u8,N,PACKED> &arr, FormatContext& ctx) const {
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

// logic
template<typename T, size_t SIZE>
struct std::formatter<cpphdl::memory_row<T,SIZE>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    template <typename FormatContext>
    auto format(const cpphdl::memory_row<T,SIZE>& row, FormatContext& ctx) const {
        auto out = ctx.out();
        out = std::format_to(out, "{}", (const cpphdl::logic<sizeof(T)*8*SIZE>)row);
        return out;
    }
};

#endif
