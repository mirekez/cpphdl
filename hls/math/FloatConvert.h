#pragma once
#include "Fp32Add.h"

namespace cpphdl::hls::math {
// Exact FP16 conversion, including subnormals and NaN payloads. Only the
// ten-bit FP16 mantissa needs normalization; no wide shifted significand.
inline uint32_t fp16_to_fp32_narrow(uint32_t raw) {
    uint32_t sign=(raw&0x8000u)<<16,exponent=(raw>>10)&31,mantissa=raw&1023;
    if(exponent==31)return sign|0x7f800000u|(mantissa<<13);
    if(exponent!=0)return sign|((exponent+112)<<23)|(mantissa<<13);
    if(!mantissa)return sign;
    uint32_t bit=fp32_leading_bit(mantissa);
    return sign|((103+bit)<<23)|((mantissa<<((23-bit)&31))&0x7fffffu);
}
// Signed int32 to FP32, round to nearest/even. Unsigned negation also handles
// INT32_MIN without C++ signed overflow. The only lossy shift is at most 8.
inline uint32_t fp32_from_int_narrow(int32_t value) {
    if(!value)return 0;
    uint32_t sign=value<0?0x80000000u:0;
    uint32_t magnitude=value<0?0u-uint32_t(value):uint32_t(value);
    uint32_t bit=fp32_leading_bit(magnitude),exponent=bit+127;
    uint32_t shift=(bit-23)&31;
    uint32_t retained=bit<=23?magnitude<<((23-bit)&31):magnitude>>shift;
    if(bit>23) {
        uint32_t tail=magnitude&((1u<<shift)-1);
        uint32_t halfway=1u<<((shift-1)&31);
        if(tail>halfway||(tail==halfway&&(retained&1)))++retained;
    }
    if(retained>=0x01000000u){retained>>=1;++exponent;}
    return sign|(exponent<<23)|(retained&0x7fffffu);
}
} // namespace cpphdl::hls::math
