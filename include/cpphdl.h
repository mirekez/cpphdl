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

#ifdef CPPHDL_STATIC  // for non static version we capture this in comb functions

#define _PORT(A...) inline static cpphdl::function_ref<A>
#define _BIND_VAR(a...)  +[]() { return &a; }  // variable
#define _BIND(a...) +[]() { static auto tmp = a; tmp = a; return &tmp; }  // expression
//#define __VAL(a...)  +[]() { static auto tmp = a; return &tmp; }  // const

#define _LAZY_COMB(name, type...) \
    type name; \
    inline static long __prev_sys_clock_##name = -1; \
    static type& name##_func() { \
        if (__prev_sys_clock_##name == sys_clock) { \
            return name; \
        } \
        __prev_sys_clock_##name = sys_clock;

#define STATIC inline static

#else  // CPPHDL_STATIC

#define _PORT(A...) cpphdl::function_ref<A>
#define _BIND_VAR(a...)  [&]() { return &a; } // variable
#define _BIND(a...) [&]() { return a; }  // expression
#define CPPHDL_UNPAREN(a...) a
#define _BIND_VAR_CAP(caps, a...)  [&, CPPHDL_UNPAREN caps]() { return &a; } // variable
#define _BIND_CAP(caps, a...) [&, CPPHDL_UNPAREN caps]() { return a; }  // expression
#define _BIND_VAR_I(a...)  [&,i]() { return &a; } // variable
#define _BIND_I(a...) [&,i]() { return a; }  // expression
#define _BIND_VAR_J(a...)  [&,j]() { return &a; } // variable
#define _BIND_J(a...) [&,j]() { return a; }  // expression
#define _BIND_VAR_IJ(a...)  [&,i,j]() { return &a; } // variable
#define _BIND_IJ(a...) [&,i,j]() { return a; }  // expression
#define _BIND_VAR_IJK(a...)  [&,i,j,k]() { return &a; } // variable
#define _BIND_IJK(a...) [&,i,j,k]() { return a; }  // expression

#define _LAZY_COMB(name, type...) \
    type name; \
    long __prev_sys_clock_##name = -1; \
    type& name##_func() { \
        if (__prev_sys_clock_##name == sys_clock) { \
            return name; \
        } \
        __prev_sys_clock_##name = sys_clock;

#define STATIC

#endif
