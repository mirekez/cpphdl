#include "ZeroConcat.cc"
using TestModule = ZeroConcat;
#ifdef TEST_RTL
#include "VZeroConcat.h"
using RtlModule = VZeroConcat;
#endif
uint64_t oracle(const uint32_t* words, unsigned selector) {
    return ((words[0] & 255u) << 8) | (selector & 255u);
}
#include "CombinationalRun.h"
