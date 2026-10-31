#pragma once
#include "FpAdd.h"
#include "Fp32Bits.h"

// Two enabled stages: FP32 product, then FpAdd's registered significand sum.
// Extra stages belong to the latency-parameterized replacement wrapper.
class Fp32Mac : public cpphdl::Module {
public:
    _PORT(uint32_t) a_in;
    _PORT(uint32_t) b_in;
    _PORT(uint32_t) c_in;
    _PORT(bool) en_in;
    _PORT(uint32_t) result_out;
    FpAdd<32,8> adder;
    cpphdl::reg<cpphdl::logic<32>> product, addend;
    uint32_t multiply_comb, result_comb;
    uint32_t& multiply_comb_func() {
        uint32_t a,b,ea,eb,ma,mb,sign,shift,retained,tail;
        uint64_t product_value,mantissa;
        int32_t exponent;
        a=a_in();b=b_in();ea=(a>>23)&255;eb=(b>>23)&255;
        ma=a&0x7fffff;mb=b&0x7fffff;sign=(a^b)&0x80000000u;
        product_value=0;mantissa=0;exponent=0;shift=20;retained=0;tail=0;
        multiply_comb=sign;
        if((ea==255&&ma)||(eb==255&&mb)||(ea==255&&!eb)||(eb==255&&!ea))
            multiply_comb=0x7fc00000u;
        else if(ea==255||eb==255) multiply_comb=sign|0x7f800000u;
        else if(ea&&eb) {
            product_value=uint64_t(ma|0x800000u)*uint64_t(mb|0x800000u);
            exponent=int32_t(ea)+int32_t(eb)-127;
            if(product_value&(uint64_t(1)<<47)){shift=21;++exponent;}
            mantissa=(product_value>>shift)|uint64_t((product_value&((uint64_t(1)<<shift)-1))!=0);
            retained=uint32_t(mantissa>>3);tail=uint32_t(mantissa)&7;
            if(tail>4||(tail==4&&(retained&1)))++retained;
            if(retained>=0x1000000u){retained>>=1;++exponent;}
            if(exponent>=255)multiply_comb=sign|0x7f800000u;
            else if(exponent>0)multiply_comb=sign|(uint32_t(exponent)<<23)|(retained&0x7fffff);
        }
        return multiply_comb;
    }
    uint32_t& result_comb_func() {
        result_comb=uint32_t(adder.c_out());
        // FpAdd's generic format uses payload 1; the HLS FP32 contract uses a
        // canonical quiet NaN. Keep the generic adder's existing ABI intact.
        if((result_comb&0x7f800000u)==0x7f800000u && (result_comb&0x7fffffu))result_comb=0x7fc00000u;
        return result_comb;
    }
    void _assign() {
        adder.a_in=_ASSIGN(cpphdl::logic<32>(product));
        adder.b_in=_ASSIGN(cpphdl::logic<32>(addend));
        adder.en_in=_ASSIGN(en_in());
        adder._assign();
        result_out=_ASSIGN_COMB(result_comb_func());
    }
    void _work(bool reset) {
        if(en_in()) { product._next=multiply_comb_func(); addend._next=c_in(); }
        if(reset) {product.clr();addend.clr();}
        adder._work(reset);
    }
    void _strobe() {product.strobe();addend.strobe();adder._strobe();}
};
