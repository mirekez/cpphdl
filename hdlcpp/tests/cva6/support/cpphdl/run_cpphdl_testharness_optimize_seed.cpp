#include "cpphdl.h"

#define private public
#include "all_generated.h"
#undef private

// The optimizer traces this concrete top; the executable main is supplied separately.
using CpphdlOptimizationTop = ariane_testharness<>;
CpphdlOptimizationTop* cpphdlOptimizationTop = nullptr;
