#pragma once

#include <iostream>
#include <string>
#if defined(CPPHDL_HAVE_STACKTRACE) && __cplusplus >= 202002L && __has_include(<stacktrace>)
#include <stacktrace>
#define STACKTRACE std::cout << std::stacktrace::current();
#else
#define STACKTRACE
#endif

extern bool cpphdlDebugEnabled;

#define DEBUG_AST(indent, a)   do { if (cpphdlDebugEnabled) { std::cout << "\n" << std::string().insert(0, (indent)*4, ' ') << a; } } while (false)
#define DEBUG_AST1(a)          do { if (cpphdlDebugEnabled) { std::cout << a; } } while (false)
#define DEBUG_EXPR(indent, a)  do { if (cpphdlDebugEnabled) { std::cout << "\n" << std::string().insert(0, (indent)*4, ' ') << a; } } while (false)
#define DEBUG_EXPR1(a)         do { if (cpphdlDebugEnabled) { std::cout << a; } } while (false)
#define DEBUG_PRJ(a)           //(a)
#define ASSERT(a) { if (!(a)) { printf("ASSERT at %s:%d\n", __FILE__, __LINE__); fflush(stdout); STACKTRACE } }
#define ASSERT1(a, b) { if (!(a)) { printf("ASSERT at %s:%d, %s\n", __FILE__, __LINE__, (b).c_str()); fflush(stdout); STACKTRACE } }
