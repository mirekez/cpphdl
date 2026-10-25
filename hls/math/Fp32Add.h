#pragma once
#include "Fp32Bits.h"

namespace cpphdl::hls::math {
// Parallel priority encoder: propagate the highest set bit downward, then
// encode its one-hot boundary. No serial count increments are needed.
inline uint32_t fp32_leading_bit(uint32_t value) {
    value|=value>>1;value|=value>>2;value|=value>>4;
    value|=value>>8;value|=value>>16;
    uint32_t one=value^(value>>1);
    return (uint32_t((one&0xffff0000u)!=0)<<4)|
           (uint32_t((one&0xff00ff00u)!=0)<<3)|
           (uint32_t((one&0xf0f0f0f0u)!=0)<<2)|
           (uint32_t((one&0xccccccccu)!=0)<<1)|
            uint32_t((one&0xaaaaaaaau)!=0);
}
// A significand with guard, round and sticky bits fits in 27 bits; its sum
// fits in 28. Keep alignment, cancellation and rounding in a 32-bit datapath.
// This is the same RNE/flush-to-zero contract as fp32_add, including NaNs,
// signed zeros and cancellation. It does not reassociate a reduction.
inline uint32_t fp32_add_narrow(uint32_t a,uint32_t b) {
    uint32_t ea=(a>>23)&255,eb=(b>>23)&255;
    uint32_t sa=a&0x80000000u,sb=b&0x80000000u;
    uint32_t ma=a&0x7fffffu,mb=b&0x7fffffu;
    if((ea==255&&ma)||(eb==255&&mb)||(ea==255&&eb==255&&sa!=sb))return 0x7fc00000u;
    if(ea==255)return a;
    if(eb==255)return b;
    if(!ea&&!eb)return sa&sb;
    if(!ea)return b;
    if(!eb)return a;
    bool exchange=(a&0x7fffffffu)<(b&0x7fffffffu);
    uint32_t large=exchange?b:a,small=exchange?a:b;
    uint32_t sign=large&0x80000000u;
    uint32_t exponent=(large>>23)&255;
    uint32_t distance=exponent-((small>>23)&255);
    uint32_t lhs=((large&0x7fffffu)|0x800000u)<<3;
    uint32_t rhs=((small&0x7fffffu)|0x800000u)<<3;
    // Mask the C++ shift count even in the unselected arm. HLS sees a
    // bounded barrel shifter and sticky-bit reduction, with no 64-bit mask.
    uint32_t shift=distance&31;
    uint32_t aligned=distance>=27?1u:(distance==0?rhs:
        ((rhs>>shift)|uint32_t((rhs<<((32-shift)&31))!=0)));
    uint32_t sum=((large^small)&0x80000000u)?lhs-aligned:lhs+aligned;
    if(sum==0)return 0;
    int16_t normalized_exponent=int16_t(exponent);
    if(sum&0x08000000u) {
        sum=(sum>>1)|(sum&1);++normalized_exponent;
    } else {
        uint32_t zeros=(26-fp32_leading_bit(sum))&31;
        sum<<=zeros;normalized_exponent-=int16_t(zeros);
    }
    uint32_t retained=(sum>>3)&0x00ffffffu,tail=sum&7;
    if(tail>4||(tail==4&&(retained&1)))++retained;
    if(retained>=0x01000000u){retained>>=1;++normalized_exponent;}
    if(normalized_exponent>=255)return sign|0x7f800000u;
    if(normalized_exponent<=0)return sign;
    return sign|(uint32_t(normalized_exponent)<<23)|(retained&0x7fffffu);
}
} // namespace cpphdl::hls::math
