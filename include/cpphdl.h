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

#ifndef CPPHDL_STATIC  // static version is faster but not used now

#define _PORT(A...) cpphdl::function_ref<A>
#define _ASSIGN(a...) [&]() { return a; }  // any expression (uses std::function, captures all object's pointers in a call chain, using heap)
#define _ASSIGN_REG(a...)  [&]() { return &a; }  // (faster) register or comb() returning ref to lvalue (uses function_ref, captures only one ref, dont use heap)

#define _ASSIGN_I(a...) [&,i]() { return a; }  // any expression
#define _ASSIGN_J(a...) [&,j]() { return a; }  // any expression
#define _ASSIGN_IJ(a...) [&,i,j]() { return a; }  // any expression
#define _ASSIGN_IJK(a...) [&,i,j,k]() { return a; }  // any expression
#define _ASSIGN_REG_I(a...)  [&,i]() { return &a; }  // (faster) register or comb() returning ref to lvalue
#define _ASSIGN_REG_J(a...)  [&,j]() { return &a; }  // (faster) register or comb() returning ref to lvalue
#define _ASSIGN_REG_IJ(a...)  [&,i,j]() { return &a; }  // (faster) register or comb() returning ref to lvalue
#define _ASSIGN_REG_IJK(a...)  [&,i,j,k]() { return &a; }  // (faster) register or comb() returning ref to lvalue

#define CPPHDL_UNPAREN(a...) a
#define _ASSIGN_CAP(caps, a...) [&, CPPHDL_UNPAREN caps]() { return a; }  // expression
#define _ASSIGN_REG_CAP(caps, a...)  [&, CPPHDL_UNPAREN caps]() { return &a; }  // (faster) register or comb returning &

// _LAZY_COMB saves some time when calling comb() 
#define _LAZY_COMB(name, type...) \
    type name; \
    long __prev_sys_clock_##name = -1; \
    type& name##_func() { \
        if (__prev_sys_clock_##name == sys_clock) { \
            return name; \
        } \
        __prev_sys_clock_##name = sys_clock;

#else  // legacy CPPHDL_STATIC - requires all methods to be static - 2 times faster but does not support arrays of modules -> not supported now

#define _PORT(A...) inline static cpphdl::function_ref<A>
#define _ASSIGN(a...) +[]() { static auto tmp = a; tmp = a; return &tmp; }  // expression
#define _ASSIGN_REG(a...)  +[]() { return &a; }  // variable

// _LAZY_COMB saves some time when calling comb() 
#define _LAZY_COMB(name, type...) \
    type name; \
    inline static long __prev_sys_clock_##name = -1; \
    static type& name##_func() { \
        if (__prev_sys_clock_##name == sys_clock) { \
            return name; \
        } \
        __prev_sys_clock_##name = sys_clock;

#endif
