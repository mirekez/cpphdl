#pragma once
#include "../Overrides.h"
#include "../../lib/math/Fp32Bits.h"
#ifndef SYNTHESIS
#include <cstring>
#endif
namespace cpphdl::hls::math {
using cpphdl_examples::math::leading_zeros;
using cpphdl_examples::math::shift_jam;
using cpphdl_examples::math::fp16_to_fp32;
using cpphdl_examples::math::fp32_round;
using cpphdl_examples::math::fp32_add;
using cpphdl_examples::math::fp32_multiply;
// Bit transport, never numerical integer/float conversion.
inline float decode(uint32_t bits) {
#ifndef SYNTHESIS
    float result; std::memcpy(&result,&bits,sizeof(result)); return result;
#else
    return 0.0f;
#endif
}
inline uint32_t encode(float value) {
#ifndef SYNTHESIS
    uint32_t result; std::memcpy(&result,&value,sizeof(result)); return result;
#else
    return 0;
#endif
}
HLS_OVERRIDE_BITS("cpphdl::hls::math::decode")
inline uint32_t decode_bits(uint32_t value) { return value; }
HLS_OVERRIDE_BITS("cpphdl::hls::math::encode")
inline uint32_t encode_bits(uint32_t value) { return value; }
HLS_OVERRIDE_BITS("@float.+.f32.f32.f32")
inline uint32_t add_bits(uint32_t a,uint32_t b) { return fp32_add(a,b); }
HLS_OVERRIDE_BITS("@float.-.f32.f32.f32")
inline uint32_t subtract_bits(uint32_t a,uint32_t b) { return fp32_add(a,b^0x80000000u); }
HLS_OVERRIDE_BITS("@float.-.f32.f32")
inline uint32_t negate_bits(uint32_t a) { return a^0x80000000u; }
HLS_OVERRIDE_BITS("@float.*.f32.f32.f32")
inline uint32_t multiply_bits(uint32_t a,uint32_t b) { return fp32_multiply(a,b); }
inline bool fp32_unordered(uint32_t a,uint32_t b) {
    return (a&0x7fffffffu)>0x7f800000u || (b&0x7fffffffu)>0x7f800000u;
}
inline bool fp32_equal(uint32_t a,uint32_t b) {
    return !fp32_unordered(a,b) && (a==b || !((a|b)&0x7fffffffu));
}
inline bool fp32_less(uint32_t a,uint32_t b) {
    if (fp32_unordered(a,b)) return false;
    if (!(a&0x7fffffffu)&&!(b&0x7fffffffu)) return false;
    if ((a^b)&0x80000000u) return (a&0x80000000u)!=0;
    return (a&0x80000000u)?a>b:a<b;
}
// IEEE ordered comparisons: NaNs compare unequal, both signed zeros compare
// equal, and finite values (including subnormals) retain their numerical order.
HLS_OVERRIDE_BITS("@float.<.f32.f32.b1")
inline bool less_bits(uint32_t a,uint32_t b) {return fp32_less(a,b);}
HLS_OVERRIDE_BITS("@float.>.f32.f32.b1")
inline bool greater_bits(uint32_t a,uint32_t b) {return fp32_less(b,a);}
HLS_OVERRIDE_BITS("@float.==.f32.f32.b1")
inline bool equal_bits(uint32_t a,uint32_t b) {return fp32_equal(a,b);}
HLS_OVERRIDE_BITS("@float.!=.f32.f32.b1")
inline bool unequal_bits(uint32_t a,uint32_t b) {return !fp32_equal(a,b);}
HLS_OVERRIDE_BITS("@float.<=.f32.f32.b1")
inline bool less_equal_bits(uint32_t a,uint32_t b) {return !fp32_unordered(a,b)&&!fp32_less(b,a);}
HLS_OVERRIDE_BITS("@float.>=.f32.f32.b1")
inline bool greater_equal_bits(uint32_t a,uint32_t b) {return !fp32_unordered(a,b)&&!fp32_less(a,b);}
inline float add(float a,float b) { return a+b; }
inline float multiply(float a,float b) { return a*b; }
inline float mac(float c,float a,float b) { return c+a*b; }
}
