#pragma once

#include "cpphdl_logic.h"

#define __PACKED __attribute__((packed))

#if defined(__has_include)
#if __has_include(<format>)
#include <format>
#endif
#else
#include <format>
#endif
#include "cpphdl_std_format.h"

//     u& operator= (const u& v) = default;

namespace cpphdl
{

template <size_t WIDTH>
struct u
{
    uint64_t value:WIDTH;
    constexpr static size_t _size_bits()
    {
        return WIDTH;
    }

    u() = default;
    constexpr u(uint64_t v) : value(v) {}
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
    u& operator&=(const u& other) { value &= other.value; return *this; }
    u& operator|=(const u& other) { value |= other.value; return *this; }
    u& operator>>=(const u& other) { value >>= other.value; return *this; }
    u& operator<<=(const u& other) { value <<= other.value; return *this; }
    constexpr operator uint64_t () { return value; }
    constexpr operator uint64_t () const { return value; }

    // Keep one implicit integral conversion for switch and overload resolution.
    // Older GCC incorrectly tries to deduce every disabled conversion template
    // in a switch condition; these exact-width convenience casts remain explicit.
    template<size_t W = WIDTH, typename std::enable_if_t<W == 8, int> = 0>
    explicit constexpr operator unsigned char() const { return static_cast<unsigned char>(value); }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 16, int> = 0>
    explicit constexpr operator unsigned short() const { return static_cast<unsigned short>(value); }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 32, int> = 0>
    explicit constexpr operator unsigned int() const { return static_cast<unsigned int>(value); }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 8, int> = 0>
    explicit constexpr operator signed char() const { return static_cast<signed char>(value); }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 16, int> = 0>
    explicit constexpr operator signed short() const { return static_cast<signed short>(value); }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 32, int> = 0>
    explicit constexpr operator signed int() const { return static_cast<signed int>(value); }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 64, int> = 0>
    explicit constexpr operator signed long() const { return static_cast<signed long>(value); }

    logic<1> operator[](size_t bit) const
    {
        return logic<1>((value >> bit) & 1u);
    }

    logic<WIDTH> bits(size_t last, size_t first)
    {
        cpphdl_assert(first < WIDTH && last < WIDTH && first <= last, "wrong first or last bitnumber");
        return logic<WIDTH>((logic<WIDTH>*)this, first, last);
    }

    constexpr operator logic<WIDTH>() const
    {
        return logic<WIDTH>(value);
    }

} __PACKED;


}

template<size_t W>
struct is_from_cpphdl_namespace<cpphdl::u<W>> : std::true_type {};

#ifdef CPPHDL_HAS_STD_FORMAT
template<size_t WIDTH>
struct std::formatter<cpphdl::u<WIDTH>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    template <typename FormatContext>
    auto format(const cpphdl::u<WIDTH> &val, FormatContext& ctx) const {
        auto out = ctx.out();
        if (WIDTH > 48) {
            out = std::format_to(out, "{:08x}", (uint64_t)val);
        } else
        if (WIDTH > 32) {
            out = std::format_to(out, "{:07x}", (uint64_t)val);
        } else
        if (WIDTH > 24) {
            out = std::format_to(out, "{:06x}", (uint32_t)val);
        } else
        if (WIDTH > 16) {
            out = std::format_to(out, "{:05x}", (uint32_t)val);
        } else
        if (WIDTH > 12) {
            out = std::format_to(out, "{:04x}", (uint16_t)val);
        } else
        if (WIDTH > 8) {
            out = std::format_to(out, "{:03x}", (uint16_t)val);
        } else
        if (WIDTH > 4) {
            out = std::format_to(out, "{:02x}", (uint8_t)val);
        }
        else {
            out = std::format_to(out, "{:x}", (uint8_t)val);
        }
        return out;
    }
};
#endif

//    class& operator= (type v) { value = v; return *this; };
//    class& operator= (const class& v) = default;

#ifdef CPPHDL_HAS_STD_FORMAT
#define DEFINE_REGULAR_TYPE_FORMATTER(type, class, text) \
template<> \
struct std::formatter<cpphdl::class> \
{ \
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); } \
    template <typename FormatContext> \
    auto format(const cpphdl::class &val, FormatContext& ctx) const { \
        auto out = ctx.out(); \
        out = std::format_to(out, text, (type)val.value); \
        return out; \
    } \
};
#else
#define DEFINE_REGULAR_TYPE_FORMATTER(type, class, text)
#endif

#define DEFINE_REGULAR_TYPE_CLASS(type, class, size, text) \
namespace cpphdl \
{ \
struct class : public u<size> \
{ \
    class() = default; \
    constexpr class(type v) : u(v) {}  \
    template<size_t W> constexpr class(u<W> v) : u((uint64_t)v) {}  \
    type* operator&() { return (type*)this; } \
    template<size_t W> explicit constexpr operator u<W>() { return value; }  \
}; \
} \
template<> \
struct is_from_cpphdl_namespace<cpphdl::class> : std::true_type {}; \
DEFINE_REGULAR_TYPE_FORMATTER(type, class, text) \
static_assert (sizeof(cpphdl::class) == (size+7)/8, "struct " #class " size is not correct");

DEFINE_REGULAR_TYPE_CLASS(bool, u1, 8, "{:1x}");
DEFINE_REGULAR_TYPE_CLASS(uint8_t, u8, 8, "{:02x}");
DEFINE_REGULAR_TYPE_CLASS(uint16_t, u16, 16, "{:04x}");
DEFINE_REGULAR_TYPE_CLASS(uint32_t, u32, 32, "{:08x}");
DEFINE_REGULAR_TYPE_CLASS(uint64_t, u64, 64, "{:016x}");
DEFINE_REGULAR_TYPE_CLASS(int8_t, i8, 8, "{:+4}");
DEFINE_REGULAR_TYPE_CLASS(int16_t, i16, 16, "{:+6}");
DEFINE_REGULAR_TYPE_CLASS(int32_t, i32, 32, "{:+11}");
DEFINE_REGULAR_TYPE_CLASS(int64_t, i64, 64, "{:+21}");

template<size_t WIDTH>
struct __ONES
{
    unsigned char data[WIDTH/8];
    constexpr __ONES() { memset(data, 0xFF, sizeof(data)); }
};

template<size_t WIDTH>
struct __ZEROS
{
    unsigned char data[WIDTH/8];
    constexpr __ZEROS() { memset(data, 0x00, sizeof(data)); }
};
