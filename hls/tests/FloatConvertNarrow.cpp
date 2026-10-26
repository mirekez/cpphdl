#include "hls/math/FloatConvert.h"
#include "hls/math/Nonlinear.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace cpphdl::hls::math;
static void integer(int32_t value) {
    if(fp32_from_int(value)!=fp32_from_int_narrow(value)) {
        std::fprintf(stderr,"int32 conversion mismatch: %d\n",value);std::exit(1);
    }
}
int main() {
    for(uint32_t bits=0;bits<65536;++bits)
        if(fp16_to_fp32(bits)!=fp16_to_fp32_narrow(bits)) {
            std::fprintf(stderr,"FP16 conversion mismatch: %04x\n",bits);return 1;
        }
    for(int64_t boundary:{0ll,1ll,8388608ll,16777216ll,33554432ll,2147483647ll,2147483648ll})
        for(int64_t offset=-16;offset<=16;++offset)for(int sign:{-1,1}) {
            int64_t value=sign*(boundary+offset);
            if(value>=INT32_MIN&&value<=INT32_MAX)integer(int32_t(value));
        }
    uint32_t state=0x497892a1;
    for(unsigned i=0;i<1000000;++i){state^=state<<13;state^=state>>17;state^=state<<5;integer(int32_t(state));}
    std::puts("PASS narrow conversions: all 65536 FP16 encodings, int32 boundaries and 1M random integers");
}
