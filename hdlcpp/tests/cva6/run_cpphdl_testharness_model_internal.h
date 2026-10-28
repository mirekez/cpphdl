#pragma once

#include "cpphdl.h"

#define private public
#include "all_generated.h"
#include "cpphdl_optimized_externs.h"
#undef private

struct CpphdlModelContext {
    cpphdl_opt_t0* dut = nullptr;
    logic<1> rstN = 0;
    logic<1> rtc = 0;
};

extern CpphdlModelContext* cpphdlModel;
