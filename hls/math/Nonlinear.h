#pragma once
#include "Float.h"
#ifndef SYNTHESIS
#include <cmath>
#endif

namespace cpphdl::hls::math {
// Same RNE/FTZ convention as Fp32Bits. Division and square root are restoring
// integer algorithms with fixed bounds; their graph can be pipelined by HLS.
inline uint32_t fp32_divide(uint32_t a, uint32_t b) {
    uint32_t ea=(a>>23)&255, eb=(b>>23)&255;
    uint32_t ma=a&0x7fffff, mb=b&0x7fffff, sign=(a^b)&0x80000000u;
    if ((ea==255&&ma)||(eb==255&&mb)||(ea==255&&eb==255)||(!ea&&!eb)) return 0x7fc00000u;
    if (ea==255||!eb) return sign|0x7f800000u;
    if (!ea||eb==255) return sign;
    ma|=0x800000; mb|=0x800000;
    int32_t exponent=int32_t(ea)-int32_t(eb)+127;
    if (ma<mb) { ma<<=1; --exponent; }
    uint32_t remainder=ma, quotient=0;
    for (unsigned i=0;i<27;++i) {
        quotient<<=1;
        if (remainder>=mb) { remainder-=mb; quotient|=1; }
        remainder<<=1;
    }
    if (remainder) quotient|=1;
    return fp32_round(sign,exponent,quotient);
}
inline uint32_t fp32_sqrt(uint32_t a) {
    uint32_t exponent=(a>>23)&255, mantissa=a&0x7fffff;
    if (!exponent) return a&0x80000000u;
    if (a&0x80000000u) return 0x7fc00000u;
    if (exponent==255) return mantissa?0x7fc00000u:0x7f800000u;
    int32_t unbiased=int32_t(exponent)-127;
    uint64_t significand=mantissa|0x800000u;
    if (unbiased%2!=0) { significand<<=1; --unbiased; }
    uint64_t radicand=significand<<25, remainder=0, root=0;
    for (unsigned i=0;i<32;++i) {
        remainder=(remainder<<2)|(radicand>>62); radicand<<=2;
        root<<=1;
        uint64_t trial=(root<<1)|1;
        if (remainder>=trial) { remainder-=trial; root|=1; }
    }
    uint64_t rounded=root>>1;
    if ((root&1)&&(remainder||(rounded&1))) ++rounded;
    int32_t result_exponent=unbiased/2+127;
    if (rounded>=0x1000000u) { rounded>>=1; ++result_exponent; }
    return (uint32_t(result_exponent)<<23)|(uint32_t(rounded)&0x7fffffu);
}
inline uint32_t fp32_from_int(int32_t value) {
    if (!value) return 0;
    uint32_t sign=value<0?0x80000000u:0;
    uint32_t magnitude=value<0?uint32_t(-int64_t(value)):uint32_t(value);
    uint32_t shift=leading_zeros(magnitude);
    uint64_t significand=uint64_t(magnitude)<<(shift+1);
    return fp32_round(sign,158-int32_t(shift),shift_jam(significand,6));
}
HLS_OVERRIDE_BITS("@float.cast.i32.f32")
inline uint32_t signed_to_float_bits(int32_t value) { return fp32_from_int(value); }
HLS_OVERRIDE_BITS("@float.cast.u32.f32")
inline uint32_t unsigned_to_float_bits(uint32_t value) {
    if (!value) return 0;
    uint32_t shift=leading_zeros(value);
    return fp32_round(0,158-int32_t(shift),shift_jam(uint64_t(value)<<(shift+1),6));
}
// Only used for exp range reduction, after its finite |x| <= 90 check.
inline int32_t fp32_small_int(uint32_t a) {
    int32_t exponent=int32_t((a>>23)&255)-127;
    uint32_t magnitude=exponent<0?0:((a&0x7fffffu)|0x800000u)>>(23-uint32_t(exponent<0?0:exponent));
    return (a&0x80000000u)?-int32_t(magnitude):int32_t(magnitude);
}
inline bool fp32_less(uint32_t a,uint32_t b) {
    if (((a>>23)&255)==255&&(a&0x7fffff)) return false;
    if (((b>>23)&255)==255&&(b&0x7fffff)) return false;
    if (!(a&0x7fffffff)&&!(b&0x7fffffff)) return false;
    if ((a^b)&0x80000000u) return (a&0x80000000u)!=0;
    return (a&0x80000000u)?a>b:a<b;
}
// Range-reduced degree-seven Taylor approximation. Error is explicitly
// tested against expf; this is not advertised as correctly rounded exp.
inline uint32_t fp32_exp(uint32_t a) {
    uint32_t magnitude=a&0x7fffffffu;
    if (magnitude>0x7f800000u) return 0x7fc00000u;
    if (magnitude>0x42b40000u) return (a&0x80000000u)?0:0x7f800000u;
    uint32_t y=fp32_multiply(a,0x3fb8aa3bu); // log2(e)
    int32_t n=fp32_small_int(fp32_add(y,(a&0x80000000u)?0xbf000000u:0x3f000000u));
    uint32_t r=fp32_add(a,fp32_multiply(fp32_from_int(n)^0x80000000u,0x3f317218u));
    uint32_t p=0x39500d01u; // 1/5040
    // Preserve the FP32 Horner order. Delayed HLS can share one iteration
    // instead of putting the complete polynomial in one controller edge;
    // streaming HLS elaborates this bounded loop into its arithmetic pipeline.
    const uint32_t coefficients[7]={0x3ab60b61u,0x3c088889u,0x3d2aaaabu,
        0x3e2aaaabu,0x3f000000u,0x3f800000u,0x3f800000u};
    for(unsigned term=0;term<7;++term)
        p=fp32_add(coefficients[term],fp32_multiply(r,p));
    int32_t exponent=int32_t((p>>23)&255)+n;
    if (exponent<=0) return 0;
    if (exponent>=255) return 0x7f800000u;
    return (p&0x7fffffu)|(uint32_t(exponent)<<23);
}
inline uint32_t fp32_silu(uint32_t a) {
    uint32_t e=fp32_exp(a|0x80000000u);
    uint32_t numerator=(a&0x80000000u)?fp32_multiply(a,e):a;
    return fp32_divide(numerator,fp32_add(0x3f800000u,e));
}
HLS_OVERRIDE_BITS("@float./.f32.f32.f32")
inline uint32_t divide_bits(uint32_t a,uint32_t b) { return fp32_divide(a,b); }
inline float exponential(float a) {
#ifndef SYNTHESIS
    return std::exp(a);
#else
    return 0;
#endif
}
inline float square_root(float a) {
#ifndef SYNTHESIS
    return std::sqrt(a);
#else
    return 0;
#endif
}
HLS_OVERRIDE_BITS("cpphdl::hls::math::exponential")
inline uint32_t exponential_bits(uint32_t a) { return fp32_exp(a); }
HLS_OVERRIDE_BITS("cpphdl::hls::math::square_root")
inline uint32_t square_root_bits(uint32_t a) { return fp32_sqrt(a); }
} // namespace cpphdl::hls::math
