#include "WideSlice.cc"
using TestModule = WideSlice;
#ifdef TEST_RTL
#include "VWideSlice.h"
using RtlModule = VWideSlice;
#endif
uint64_t oracle(const uint32_t* words, unsigned offset) {
    uint64_t result = 0;
    for (unsigned bit = 0; bit < 64; ++bit) {
        unsigned source = offset + bit;
        result |= uint64_t((words[source / 32] >> (source % 32)) & 1) << bit;
    }
    return result ^ (uint64_t(words[1]) | (uint64_t(words[2]) << 32));
}
#include "CombinationalRun.h"
