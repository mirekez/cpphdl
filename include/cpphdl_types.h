#pragma once

#include "cpphdl_logic.h"

#include <stdint.h>

#define __PACKED __attribute__((packed))

#if __cplusplus >= 202302L
#define USE_FORMAT_H  // for c++26
#endif

#ifdef USE_FORMAT_H
#include <format>
#include <print>
#else
namespace std {

template<typename T>
struct formatter {};
struct format_parse_context { constexpr bool begin() { return true; } };
template<typename T>
bool format_to(bool, const char*, T& t)
{
    return false;
}

}
#endif

namespace cpphdl
{

template <size_t WIDTH>
struct u
{
    uint64_t value:WIDTH;
    u() = default;
    u(uint64_t v) : value(v) {}
    u& operator= (const u& v) = default;
    u& operator= (uint64_t v) { value = v; return *this; };
//    u* operator&() { return (u*)this; }
    u& operator++() { value = value+1; return *this; }
    u operator++(int) { u temp = *this; value = value+1; return temp; }
    u& operator--() { value = value-1; return *this; }
    u operator--(int) { u temp = *this; value = value-1; return temp; }
    u& operator+=(const u& other) { value += other.value; return *this; }
    u& operator-=(const u& other) { value -= other.value; return *this; }
    u& operator*=(const u& other) { value *= other.value; return *this; }
    u& operator/=(const u& other) { value /= other.value; return *this; }
    operator uint64_t () { return value; }
    operator uint64_t () const { return value; }

    logic<WIDTH> bits(size_t last, size_t first)
    {
        cpphdl_assert(first < WIDTH && last < WIDTH && first <= last, "wrong first or last bitnumber");
        return logic<WIDTH>((logic<WIDTH>*)this, first, last);
    }

    operator logic<WIDTH>()
    {
        logic<WIDTH> bs;
        memcpy(&bs, value, sizeof(bs));
    }
} __PACKED;

}

template<size_t W>
struct is_from_cpphdl_namespace<cpphdl::u<W>> : std::true_type {};

template<size_t WIDTH>
struct std::formatter<cpphdl::u<WIDTH>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    template <typename FormatContext>
    auto format(const cpphdl::u<WIDTH> &val, FormatContext& ctx) const {
        auto out = ctx.out();
        if (WIDTH > 12) {
            out = std::format_to(out, "{:04x}", static_cast<const uint64_t&>(val));
        } else
        if (WIDTH > 8) {
            out = std::format_to(out, "{:03x}", static_cast<const uint64_t&>(val));
        } else
        if (WIDTH > 4) {
            out = std::format_to(out, "{:02x}", static_cast<const uint64_t&>(val));
        }
        else {
            out = std::format_to(out, "{:x}", static_cast<const uint64_t&>(val));
        }
        return out;
    }
};

#define DEFINE_REGULAR_TYPE_CLASS(type, class, size, text) \
namespace cpphdl \
{ \
struct class : public u<size> \
{ \
    class() = default; \
    class(type v) : u(v) {}  \
    class& operator= (const class& v) = default; \
    class& operator= (type v) { value = v; return *this; }; \
    type* operator&() { return (type*)this; } \
}; \
} \
template<> \
struct is_from_cpphdl_namespace<cpphdl::class> : std::true_type {}; \
template<> \
struct std::formatter<cpphdl::class> \
{ \
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); } \
    template <typename FormatContext> \
    auto format(const cpphdl::class &val, FormatContext& ctx) const { \
        auto out = ctx.out(); \
        out = std::format_to(out, text, static_cast<const type&>(val.value)); \
        return out; \
    } \
}; \
static_assert (sizeof(cpphdl::class) == (size+7)/8, "struct " #class " size is not correct");

DEFINE_REGULAR_TYPE_CLASS(bool, u1, 8, "{:1x}");
DEFINE_REGULAR_TYPE_CLASS(uint8_t, u8, 8, "{:02x}");
DEFINE_REGULAR_TYPE_CLASS(uint16_t, u16, 16, "{:04x}");
DEFINE_REGULAR_TYPE_CLASS(uint32_t, u32, 32, "{:08x}");
DEFINE_REGULAR_TYPE_CLASS(uint64_t, u64, 64, "{:016x}");
DEFINE_REGULAR_TYPE_CLASS(int8_t, s8, 8, "{:+4}");
DEFINE_REGULAR_TYPE_CLASS(int16_t, s16, 16, "{:+6}");
DEFINE_REGULAR_TYPE_CLASS(int32_t, s32, 32, "{:+11}");
DEFINE_REGULAR_TYPE_CLASS(int64_t, s64, 64, "{:+21}");
