#include "../Clocked.h"

uint32_t pipeline_global;
uint64_t unavailable(uint32_t);
struct
#if PIPELINE_REJECT == 12 && defined(__clang__)
[[clang::annotate("CPPHDL_KEEP_BOX=1.2")]]
#endif
PipelineUnsupported {
#if PIPELINE_REJECT == 2
    uint32_t values[32] = {};
#endif
    uint32_t helper(uint32_t value) {
        for (uint32_t i = 0; i < value; ++i) value += 3;
        return value;
    }
#if PIPELINE_REJECT == 11 && defined(__clang__)
    [[clang::annotate("CPPHDL_ONE_CLOCK")]]
#endif
#if PIPELINE_REJECT == 13
    uint64_t command(uint64_t op, uint64_t index, uint64_t value) {
#elif PIPELINE_REJECT == 14
    __uint128_t command(uint32_t op, uint32_t index, uint32_t value) {
#elif PIPELINE_REJECT == 15
    uint64_t command(uint32_t op, uint16_t index, uint32_t value) {
#else
    uint64_t command(uint32_t op, uint32_t index, uint32_t value) {
#endif
#if PIPELINE_REJECT == 0
        while (index) { value += index; --index; }
        return value;
#elif PIPELINE_REJECT == 1
        return helper(value);
#elif PIPELINE_REJECT == 2
        values[index & 31] = value;
        return values[op & 31];
#elif PIPELINE_REJECT == 3
        return index ? command(op,index-1,value) : value;
#elif PIPELINE_REJECT == 4
        static uint32_t local;
        return local += value;
#elif PIPELINE_REJECT == 5
        auto* pointer = new uint32_t(value);
        uint32_t result = *pointer;
        delete pointer;
        return result;
#elif PIPELINE_REJECT == 6
        return pipeline_global += value;
#elif PIPELINE_REJECT == 8
        return uint64_t(float(value) * 1.25f);
#elif PIPELINE_REJECT == 9
        return unavailable(value);
#elif PIPELINE_REJECT == 16 || PIPELINE_REJECT == 17
        return cpphdl::hls::external_memory<uint64_t>(index)[value];
#else
        return value;
#endif
    }
};
class PipelineUnsupportedTop : public cpphdl::Module {
public:
#if PIPELINE_REJECT == 7
    cpphdl::hls::ClockedPipeline<PipelineUnsupported,0> worker;
#elif PIPELINE_REJECT == 10
    cpphdl::hls::ClockedPipeline<PipelineUnsupported,65> worker;
#elif PIPELINE_REJECT == 17
    cpphdl::hls::ClockedDelayer<PipelineUnsupported> worker;
#else
    cpphdl::hls::ClockedPipeline<PipelineUnsupported,3> worker;
#endif
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};
