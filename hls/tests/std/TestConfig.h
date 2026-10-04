#pragma once

#ifndef HLS_CAPACITY
#define HLS_CAPACITY 8
#endif
static_assert(HLS_CAPACITY > 0, "container test requires a capacity");

#define HLS_STRING_IMPL(x) #x
#define HLS_STRING(x) HLS_STRING_IMPL(x)
#if defined(__clang__) && !defined(HLS_NO_CAPACITY)
#define HLS_BOUNDED(n) [[clang::annotate("CPPHDL_HLS_CAPACITY=" HLS_STRING(n))]]
#else
#define HLS_BOUNDED(n)
#endif
