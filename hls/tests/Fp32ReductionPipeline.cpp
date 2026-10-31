#include "../Clocked.h"
#include "../math/Reduction.h"
#ifndef REDUCTION_ROWS
#define REDUCTION_ROWS 2
#endif
#ifndef REDUCTION_COLS
#define REDUCTION_COLS 3
#endif
using ReductionInput=cpphdl::hls::math::MatrixValues<uint32_t,REDUCTION_ROWS,REDUCTION_COLS>;
using ReductionOutput=cpphdl::hls::math::VectorValues<uint32_t,REDUCTION_ROWS>;
cpphdl::hls::ClockedPipeline<cpphdl::hls::math::ReductionMethods<REDUCTION_ROWS,REDUCTION_COLS>,3,
    ReductionInput,ReductionOutput> reduction_worker;
