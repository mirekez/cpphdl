#pragma once

#include <iostream>

#define DEBUG_AST(a) (a)
#define DEBUG_PRJ(a) (a)
#define ASSERT(a) { if (!(a)) { printf("ASSERT at %s:%d\n", __FILE__, __LINE__); fflush(stdout); } }
