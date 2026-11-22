#pragma once
#include "../../lib/math/Fp32Bits.h"
#include <array>
#include <cstdint>
#include <cstring>
#if LOOP_CASE==12
inline constexpr uint32_t comparisonBits[]={0,0x80000000u,0x3f800000u,0xbf800000u,
    0x7f800000u,0xff800000u,0x7fc00001u,1,0x80000001u};
#endif
inline uint32_t loopValue(unsigned id) {
#if LOOP_CASE==12
    return comparisonBits[(id/9)%9];
#endif
    return LOOP_CASE==1 || LOOP_CASE==2 || LOOP_CASE==7 || LOOP_CASE==13?0x3d000000u|((id*0x9e3779b9u)&0x01ffffffu):id*97u+3u;
}
inline std::array<uint32_t,64> loopInitial() {
    std::array<uint32_t,64> memory{};
    for(unsigned i=0;i<memory.size();++i)
#if LOOP_CASE==12
        memory[i]=comparisonBits[i%9];
#else
        memory[i]=LOOP_CASE==1 || LOOP_CASE==2 || LOOP_CASE==7 || LOOP_CASE==13?0x3d000000u|((i*0x9e3779b9u)&0x01ffffffu):i*13u+7u;
#endif
    return memory;
}
template<class Read,class Write>
uint64_t loopReference(unsigned count,uint32_t value,Read read,Write write) {
    uint32_t sum=value;
#if LOOP_CASE==0
    for(unsigned i=0;i<count;++i)sum+=read(i);
#elif LOOP_CASE==1 || LOOP_CASE==2 || LOOP_CASE==7
    std::array<uint32_t,8> cached{};
    if(LOOP_CASE==7)for(unsigned i=0;i<count;++i)cached[i]=read(i);
    for(unsigned i=0;i<(LOOP_CASE==1?4:count);++i) {
        // Integer IEEE implementation is independent of the source's native
        // floating point and the simulation replacement's real arithmetic.
        uint32_t a=LOOP_CASE==7?cached[i]:read(i),b=read(16+i);
        sum=cpphdl_examples::math::fp32_add(sum,cpphdl_examples::math::fp32_multiply(a,b));
    }
#elif LOOP_CASE==3
    for(unsigned row=0;row<count;++row) {
        unsigned col=0;
        while(col<(value&3)) {
            if(col!=1){sum+=read(4*row+col);if(sum&8)break;}
            ++col;
        }
    }
#elif LOOP_CASE==4
    for(unsigned i=0;i<count;++i) {
        uint32_t x=read(i);
        if(!(x&1)){sum+=x;if(sum&4)break;}
    }
    sum=(sum+4u)&~3u;
#elif LOOP_CASE==6
    for(unsigned i=0;i<count;++i)sum+=read(i)+value;
#elif LOOP_CASE==8
    for(unsigned i=0;i<count;++i)sum+=(read(i)&65535u)|((value+i)<<16);
#elif LOOP_CASE==9
    sum=0xdf800000u^value;
    write(32,sum);return (uint64_t(0x5f800000u)<<32)|sum;
#elif LOOP_CASE==10
    sum=3*value+3;
    write(32,sum);return (uint64_t(value+1)<<32)|sum;
#elif LOOP_CASE==11
    const uint32_t bits[]={0x80000000u,0x3f800000u,0x3727c5acu};
    sum=bits[count<3?count:2]^value;
    write(32,sum);return (uint64_t(0x3727c5acu)<<32)|(sum^0x3727c5acu);
#elif LOOP_CASE==12
    uint32_t bits=read(count);float a,b;
    std::memcpy(&a,&bits,4);std::memcpy(&b,&value,4);
    sum=uint32_t(a<b)|(uint32_t(a>b)<<1)|(uint32_t(a==b)<<2)|
        (uint32_t(a!=b)<<3)|(uint32_t(a<=b)<<4)|(uint32_t(a>=b)<<5);
#elif LOOP_CASE==13
    uint32_t result[4];
    for(unsigned lane=0;lane<4;++lane) {
        uint32_t a=read(lane),b=read(16+lane);
        result[lane]=lane<count?cpphdl_examples::math::fp32_add(value,cpphdl_examples::math::fp32_multiply(a,b)):value;
    }
    for(unsigned lane=0;lane<4;++lane)write(32+lane,result[lane]);
    return (uint64_t(result[3])<<32)|result[0];
#elif LOOP_CASE==14
    for(unsigned i=0;i<count;++i)sum+=read(i);
    write(32,sum);return (uint64_t(0x1000+4*count)<<32)|sum;
#elif LOOP_CASE==15
    sum=0;
    for(unsigned i=0;i<4;++i){auto updated=read(i)+value;write(i,updated);sum+=updated;}
#elif LOOP_CASE==16
    for(unsigned i=0;i<count;++i) {
        auto word=read(i);if(!(word&1u))break;
        // The body is a separate source region from the memory-reading test.
        sum+=read(i);
    }
#elif LOOP_CASE==17
    for(unsigned i=0;i<count;++i)if(i!=1)sum+=read(i);
    write(32,sum);return (uint64_t(count+1)<<32)|sum;
#elif LOOP_CASE==18
    std::array<uint32_t,8> data{};
    for(unsigned i=0;i<8;++i)data[i]=read(i);
    for(unsigned round=0;round<count;++round)
        for(unsigned lane=0;lane<4;++lane)
            sum+=lane<count?data[(round+lane)%8]+data[(round+lane+1)%8]:value;
#elif LOOP_CASE==19
    std::array<uint32_t,8> data{};
    for(unsigned i=0;i<8;++i)data[i]=read(i);
    for(unsigned round=0;round<count;++round) {
        uint32_t next=data[data[round%8]%8]+value;
        data[(round+1)%8]=next;sum+=next;
    }
#elif LOOP_CASE==20
    std::array<uint32_t,4> data{};
    for(unsigned i=0;i<count;++i)data[(i+value)%4]=read(i)+i;
    for(auto word:data)sum=sum*33+word;
#elif LOOP_CASE==21
    std::array<uint32_t,8> data{};
    for(unsigned i=0;i<count;++i) {
        data[i]=read(i);if(data[i]&1)break;
    }
    for(unsigned i=0;i<count;++i)sum+=data[i];
#elif LOOP_CASE==22
    sum=value+count;
#elif LOOP_CASE==23
    // Distinct locations for the at-most-eight writes, all other words zero.
    for(unsigned i=0;i<count;++i)sum+=read(i);
#else
    for(unsigned i=0;i<((count&1)?count:1);++i)sum+=read(i);
    sum+=(count&1)?11u:23u;
    uint32_t first=sum*3u+value,second=(sum+1u)*3u+value;
    write(32,first);return (uint64_t(second)<<32)|first;
#endif
    write(32,sum);return sum;
}
