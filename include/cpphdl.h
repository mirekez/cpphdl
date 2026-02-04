#pragma once

//#include <stdint.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned uint32_t;
typedef unsigned long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;

#include <type_traits>
#include <string>

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

template<typename T>
struct is_from_cpphdl_namespace : std::false_type {};

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
#include "cpphdl_Model.h"
#include "cpphdl_port.h"
//inline static cpphdl::function_ref<A>
#define __PORT(A...) inline static cpphdl::function_ref<A>
#define __VAR(a...)  +[]() { return a; }  // variable
#define __EXPR(a...) +[]() { static auto tmp = a; tmp = a; return &tmp; }  // expression
#define __VAL(a...)  +[]() { static auto tmp = a; return &tmp; }  // const

#define __LAZY_COMB(name, type...) \
    inline static unsigned long __prev_sys_clock_##name = -1; \
    static type name##_func() { \
        if (sys_clock == __prev_sys_clock_##name) { \
            return name; \
        } \
        __prev_sys_clock_##name = sys_clock;

#define STATIC inline static
