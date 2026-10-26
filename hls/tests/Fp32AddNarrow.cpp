#include "hls/math/Fp32Add.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static void check(uint32_t a,uint32_t b) {
    using namespace cpphdl::hls::math;
    uint32_t expected=fp32_add(a,b),actual=fp32_add_narrow(a,b);
    if(expected!=actual) {
        std::fprintf(stderr,"add(%08x,%08x): %08x != %08x\n",a,b,actual,expected);
        std::exit(1);
    }
}
int main() {
    // Special classes, cancellation, rounding ties and every exponent gap.
    const uint32_t edge[]={0,0x80000000,1,0x007fffff,0x00800000,0x00800001,
        0x3f000000,0x3f800000,0x3f800001,0x3fffffff,0x40000000,
        0x7f7fffff,0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xffa12345};
    for(auto a:edge)for(auto b:edge){check(a,b);check(a^0x80000000u,b);}
    for(uint32_t ea=1;ea<255;++ea)for(uint32_t eb=1;eb<255;++eb)
        for(uint32_t mant: {0u,1u,0x3fffffu,0x7ffffeu,0x7fffffu}) {
            uint32_t a=(ea<<23)|mant,b=(eb<<23)|(0x7fffffu-mant);
            check(a,b);check(a,b^0x80000000u);check(b,a);check(b^0x80000000u,a);
            check(a,a^0x80000000u);check(a,(a-1)^0x80000000u);
        }
    uint32_t random=0x17982ab5u;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<2000000;++i){uint32_t a=next(),b=next();check(a,b);}
    std::puts("PASS narrow FP32 add: exact original contract, classes, exponent gaps, cancellation and 2M random pairs");
}
