#pragma once

#include <iostream>
#include <stacktrace>
#define DEBUG_AST(indent, a)  // (std::cout << "\n" << std::string().insert(0, (indent)*4, ' ') << a)
#define DEBUG_AST1(a)         // (std::cout << a)
#define DEBUG_EXPR(indent, a) // (std::cout << "\n" << std::string().insert(0, (indent)*4, ' ') << a)
#define DEBUG_EXPR1(a)        // (std::cout << a)
#define DEBUG_PRJ(a)          // (a)
#define ASSERT(a) { if (!(a)) { printf("ASSERT at %s:%d\n", __FILE__, __LINE__); fflush(stdout); std::cout << std::stacktrace::current(); } }
#define ASSERT1(a, b) { if (!(a)) { printf("ASSERT at %s:%d, %s\n", __FILE__, __LINE__, (b).c_str()); fflush(stdout); std::cout << std::stacktrace::current(); } }
