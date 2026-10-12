#pragma once
#include "../Overrides.h"
#include "Fp32Bits.h"
#ifndef SYNTHESIS
#include <cstring>
#endif
namespace cpphdl::hls::math {
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
inline float add(float a,float b) { return a+b; }
inline float multiply(float a,float b) { return a*b; }
inline float mac(float c,float a,float b) { return c+a*b; }
}
