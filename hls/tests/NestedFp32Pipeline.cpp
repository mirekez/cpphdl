#include "../Clocked.h"
#include "../math/Fp32MacPipeline.h"

struct PipelineMethods {
    uint64_t command(uint32_t c,uint32_t tag,uint32_t a) const {
        uint32_t bits=cpphdl::hls::math::Fp32MacMethods{}.command(c,a,0x3f000000u);
        return (uint64_t(tag)<<32)|bits;
    }
};

#if defined(VERILATOR) && !defined(SYNTHESIS)
#include "../../lib/math/Fp32Bits.h"
#define PIPELINE_REFERENCE(CANDIDATE,C,IDX,A) \
    ((uint64_t(IDX)<<32)|cpphdl_examples::math::fp32_add( \
        (C),cpphdl_examples::math::fp32_multiply((A),0x3f000000u)))
#endif

#define PIPELINE_CUSTOM_METHODS
#include "Pipeline.cpp"
