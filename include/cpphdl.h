#pragma once

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned uint32_t;
typedef unsigned long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;

namespace cpphdl
{


typedef unsigned char byte;

constexpr unsigned flog2(unsigned x)
{
    return x == 1 ? 0 : 1+flog2(x >> 1);
}

constexpr unsigned clog2(unsigned x)
{
    return x == 1 ? 0 : flog2(x - 1) + 1;
}


}

#include <type_traits>
#include <array>
#include <initializer_list>

template<typename T>
struct is_from_cpphdl_namespace : std::false_type {};

#include <string>

struct cpphdl_exception
{
    std::string text;
};

#define cpphdl_assert(a, text) if (!(a)) { throw cpphdl_exception{text}; }

#include "cpphdl_types.h"
#include "cpphdl_array.h"
#include "cpphdl_logic.h"
#include "cpphdl_reg.h"
#include "cpphdl_format.h"
#include "cpphdl_cat.h"
#include "cpphdl_memory.h"
#include "cpphdl_module.h"
#include "cpphdl_port.h"

namespace cpphdl
{






template<typename T, typename V>
constexpr T sv_cast(const V& value)
{
    T out{};
    out = value;
    return out;
}

template<size_t WIDTH>
constexpr logic<WIDTH> byteswap(const logic<WIDTH>& value)
{
    logic<WIDTH> out = 0;
    constexpr size_t byte_count = (WIDTH + 7) / 8;
    for (size_t byte = 0; byte < byte_count; ++byte) {
        for (size_t bit = 0; bit < 8; ++bit) {
            size_t src = byte * 8 + bit;
            size_t dst = (byte_count - 1 - byte) * 8 + bit;
            if (src < WIDTH && dst < WIDTH) {
                out.set(dst, value.get(src));
            }
        }
    }
    return out;
}

template<size_t WIDTH>
constexpr logic<WIDTH> byteswap(const logic_bits<WIDTH>& value)
{
    return byteswap(static_cast<const logic<WIDTH>&>(value));
}

template<typename T, size_t WIDTH, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
constexpr T operator&(T lhs, const logic<WIDTH>& rhs)
{
    return static_cast<T>(static_cast<uint64_t>(lhs) & static_cast<uint64_t>(rhs));
}

template<typename T, size_t WIDTH, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
constexpr T operator|(T lhs, const logic<WIDTH>& rhs)
{
    return static_cast<T>(static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs));
}

template<typename T, size_t WIDTH, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
constexpr T operator^(T lhs, const logic<WIDTH>& rhs)
{
    return static_cast<T>(static_cast<uint64_t>(lhs) ^ static_cast<uint64_t>(rhs));
}

template<typename T>
struct type_width_value
{
    static constexpr size_t value = sizeof(T) * 8;
};

template<size_t WIDTH>
struct type_width_value<logic<WIDTH>>
{
    static constexpr size_t value = WIDTH;
};

template<typename T, size_t N, bool PACKED>
struct type_width_value<array<T, N, PACKED>>
{
    static constexpr size_t value = array<T, N, PACKED>::_size_bits();
};

template<typename T>
constexpr size_t type_width()
{
    return type_width_value<std::remove_cv_t<std::remove_reference_t<T>>>::value;
}

template<size_t COUNT, size_t WIDTH>
constexpr logic<COUNT * WIDTH> repeat(const logic<WIDTH>& value)
{
    logic<COUNT * WIDTH> out = 0;
    for (size_t rep = 0; rep < COUNT; ++rep) {
        for (size_t bit = 0; bit < WIDTH; ++bit) {
            out.set(rep * WIDTH + bit, value.get(bit));
        }
    }
    return out;
}

template<size_t WIDTH>
constexpr logic<1> reduce_and(const logic<WIDTH>& value)
{
    for (size_t bit = 0; bit < WIDTH; ++bit) {
        if (!value.get(bit)) {
            return logic<1>(0);
        }
    }
    return logic<1>(1);
}

template<typename T, size_t N, bool PACKED>
constexpr logic<1> reduce_and(const array<T, N, PACKED>& value)
{
    for (size_t i = 0; i < N; ++i) {
        if (!static_cast<bool>(value[i])) {
            return logic<1>(0);
        }
    }
    return logic<1>(1);
}

template<typename T, typename V>
constexpr void sv_assign_field(T& dst, const V& value)
{
    if constexpr (std::is_arithmetic_v<T> && !std::is_arithmetic_v<std::remove_cv_t<std::remove_reference_t<V>>>) {
        dst = static_cast<T>(value);
    }
    else {
        dst = value;
    }
}

template<typename T, size_t N, typename V>
constexpr void sv_assign_field(std::array<T, N>& dst, std::initializer_list<V> values)
{
    size_t i = 0;
    for (const auto& value : values) {
        if (i >= N) {
            break;
        }
        sv_assign_field(dst[i++], value);
    }
}

template<typename T, size_t N, typename V>
constexpr void sv_assign_field(std::array<T, N>& dst, const V& value)
{
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<V>>, std::array<T, N>>) {
        dst = value;
    }
    else {
        for (auto& item : dst) {
            sv_assign_field(item, value);
        }
    }
}

template<typename T, size_t N, bool PACKED, typename V>
constexpr void sv_assign_field(array<T, N, PACKED>& dst, const V& value)
{
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<V>>, array<T, N, PACKED>>) {
        dst = value;
    }
    else {
        for (size_t i = 0; i < N; ++i) {
            sv_assign_field(dst[i], value);
        }
    }
}

}

#ifndef CPPHDL_STATIC  // static version is faster but not used now

#define _PORT(A...)  cpphdl::function_ref<A>
#define _ASSIGN(a...)  [&]() { return a; }  // any expression (uses std::function, captures all object's pointers in a call chain, using heap)
#define _ASSIGN_REG(a...)  [&]() { return &a; }  // (faster) register or comb() returning ref to lvalue (uses function_ref, captures only one ref, dont use heap)
#define _ASSIGN_COMB(a...)  _ASSIGN_REG(a)  // all comb functions return reference to lvalue in cpphdl, but keep called when being accessed

#define _ASSIGN_I(a...)  [&,i]() { return a; }  // any expression
#define _ASSIGN_J(a...)  [&,j]() { return a; }
#define _ASSIGN_IJ(a...)  [&,i,j]() { return a; }
#define _ASSIGN_REG_I(a...)  [&,i]() { return &a; }  // (faster) register or comb() returning ref to lvalue
#define _ASSIGN_REG_J(a...)  [&,j]() { return &a; }
#define _ASSIGN_REG_IJ(a...)  [&,i,j]() { return &a; }
#define _ASSIGN_COMB_I(a...)  _ASSIGN_REG_I(a)  // all comb functions return reference to lvalue
#define _ASSIGN_COMB_J(a...)  _ASSIGN_REG_J(a)
#define _ASSIGN_COMB_IJ(a...)  _ASSIGN_REG_IJ(a)

// captures any indexes: _ASSIGN_INDEXED((i,j,k), out_reg[i+j+k])
#define CPPHDL_UNPAREN(a...) a
#define _ASSIGN_INDEXED(caps, a...) [&, CPPHDL_UNPAREN caps]() { return a; }  // expression
#define _ASSIGN_REG_INDEXED(caps, a...)  [&, CPPHDL_UNPAREN caps]() { return &a; }  // (faster) register or comb returning &
#define _ASSIGN_COMB_INDEXED(a...)  _ASSIGN_REG_INDEXED(a)

// _LAZY_COMB saves some time when calling comb() 
#define _LAZY_COMB(name, type...) \
    type name; \
    long __prev__system_clock_##name = -1; \
    type& name##_func() { \
        if (__prev__system_clock_##name == _system_clock) { \
            return name; \
        } \
        __prev__system_clock_##name = _system_clock;

#else  // legacy CPPHDL_STATIC - requires all methods to be static - 2 times faster but does not support arrays of modules -> not supported now

#define _PORT(A...) inline static cpphdl::function_ref<A>
#define _ASSIGN(a...) +[]() { static auto tmp = a; tmp = a; return &tmp; }  // expression
#define _ASSIGN_REG(a...)  +[]() { return &a; }  // variable
#define _ASSIGN_COMB(a...)  _ASSIGN_REG(a)

// _LAZY_COMB saves some time when calling comb() 
#define _LAZY_COMB(name, type...) \
    inline static type name; \
    inline static long __prev__system_clock_##name = -1; \
    static type& name##_func() { \
        if (__prev__system_clock_##name == _system_clock) { \
            return name; \
        } \
        __prev__system_clock_##name = _system_clock;

#endif
