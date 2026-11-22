#include "../Clocked.h"
#ifndef LOOP_CASE
#define LOOP_CASE 2
#endif
#if LOOP_CASE==15
struct LaneMemory {
    cpphdl::hls::external_ptr<uint32_t> data;
    uint32_t read(uint32_t i) const {return data[i];}
    void write(uint32_t i,uint32_t value) const {data[i]=value;}
};
#endif
#if LOOP_CASE==1 || LOOP_CASE==2 || LOOP_CASE==7 || LOOP_CASE==13
#include "../math/Fp32MacPipeline.h"
#endif
#if LOOP_CASE==13
struct FourFloats {float lane[4];};
FourFloats parallel_mac(FourFloats a,FourFloats b,FourFloats c,uint32_t active) {
    FourFloats result{};
#pragma omp simd
    for(unsigned lane=0;lane<4;++lane)
        result.lane[lane]=lane<active?
            cpphdl::hls::math::Fp32MacMethods::calculate(c.lane[lane],a.lane[lane],b.lane[lane]):c.lane[lane];
    return result;
}
#endif
#if LOOP_CASE==9 || LOOP_CASE==11 || LOOP_CASE==12
#include "../math/Float.h"
#endif
#if LOOP_CASE==11
struct FloatConstants {
    static constexpr float epsilon=1e-5f,negative=-3.25f,negative_zero=-0.0f;
    static constexpr double double_zero=-0.0;
};
struct FloatTable {float value[3];};
constexpr FloatTable float_table{{-0.0f,1.0f,1e-5f}};
#endif
#if LOOP_CASE==10
struct LocalTriple {uint32_t word[3];};
struct LocalRecord {uint32_t* pointer;uint32_t flag;};
#endif
#if LOOP_CASE==5
#include "NestedAtomic.h"
struct Finished {uint32_t lane[2];};
Finished finish_sum(uint32_t sum,uint32_t bias) {
    Finished result{};
#pragma omp simd
    for(unsigned lane=0;lane<2;++lane)result.lane[lane]=uint32_t(NestedAtomic{}.command(sum+lane,bias,3));
    return result;
}
#endif
#if LOOP_CASE==1 || LOOP_CASE==2 || LOOP_CASE==7
template<class Left,class Right>
float dot_product(Left a,Right b,uint32_t count,float initial) {
    float sum=initial;
    for(uint32_t i=0;i<count;++i) {
        float av=a[i];float bv=b[i];
        sum=cpphdl::hls::math::Fp32MacMethods::calculate(sum,av,bv);
    }
    return sum;
}
#endif
#if LOOP_CASE==11
template<class Config>
#endif
struct SourceLoopMethods {
    uint64_t command(uint32_t base,uint32_t count,uint32_t value) const {
        auto memory=cpphdl::hls::external_memory<uint32_t>(base);
#if LOOP_CASE==0
        uint32_t sum=value;
        for(uint32_t i=0;i<count;++i)sum+=memory[i];
        memory[32]=sum;return sum;
#elif LOOP_CASE==1 || LOOP_CASE==2
        auto floats=cpphdl::hls::external_memory<float>(base);
#if LOOP_CASE==1
        float sum=dot_product(floats,floats+16,4,cpphdl::hls::math::decode(value));
#else
        float sum=dot_product(floats,floats+16,count,cpphdl::hls::math::decode(value));
#endif
        floats[32]=sum;return cpphdl::hls::math::encode(sum);
#elif LOOP_CASE==3
        uint32_t sum=value;
        for(uint32_t row=0;row<count;++row) {
            for(uint32_t col=0;col<(value&3);++col) {
                if(col==1)continue;
                sum+=memory[4*row+col];
                if(sum&8)break;
            }
        }
        memory[32]=sum;return sum;
#elif LOOP_CASE==4
        uint32_t sum=value,i=0;
        while(i<count) {
            uint32_t x=memory[i++];
            if(x&1)continue;
            sum+=x;if(sum&4)break;
        }
        do {++sum;} while((sum&3)!=0);
        memory[32]=sum;return sum;
#elif LOOP_CASE==6
        uint32_t scratch[256]{};
        for(uint32_t i=0;i<count;++i)scratch[17*i]=memory[i]+value;
        uint32_t sum=value;
        for(uint32_t i=count;i>0;--i)sum+=scratch[17*(i-1)];
        // Untouched elements must be cleared again for each invocation.
        sum+=scratch[17*count];
        memory[32]=sum;return sum;
#elif LOOP_CASE==7
        auto floats=cpphdl::hls::external_memory<float>(base);
        float scratch[256];
        for(uint32_t i=0;i<count;++i)scratch[i]=floats[i];
        float sum=dot_product(scratch,floats+16,count,cpphdl::hls::math::decode(value));
        floats[32]=sum;return cpphdl::hls::math::encode(sum);
#elif LOOP_CASE==8
        uint32_t scratch[256];
        for(uint32_t i=0;i<count;++i)scratch[2*i]=memory[i];
        auto halves=reinterpret_cast<uint16_t*>(scratch);
        uint32_t sum=value;
        for(uint32_t i=0;i<count;++i) {
            halves[4*i+1]=uint16_t(value+i);
            sum+=scratch[2*i];
        }
        memory[32]=sum;return sum;
#elif LOOP_CASE==9
        using cpphdl::hls::math::encode;
        // No runtime conversion override is needed for constant C++ casts.
        uint32_t wide=encode(float(18446744073709551615ull));
        uint32_t sum=encode(float(0))^encode(float(16777217u))^encode(float(-16777217))^wide^value;
        memory[32]=sum;return (uint64_t(wide)<<32)|sum;
#elif LOOP_CASE==10
        LocalTriple triples[128];
        LocalTriple original{{value,value+1,value+2}};
        triples[count]=original;
        LocalTriple gathered=triples[count];
        uint32_t scratch[256];scratch[count]=gathered.word[2];
        LocalRecord records[128];
        LocalRecord record{scratch+count,gathered.word[1]};
        records[count]=record;
        LocalRecord copied=records[count];
        *copied.pointer+=copied.flag;
        uint32_t sum=*copied.pointer+gathered.word[0];
        memory[32]=sum;return (uint64_t(copied.flag)<<32)|sum;
#elif LOOP_CASE==11
        using cpphdl::hls::math::encode;
        const float* address=&Config::epsilon;
        const float* table=float_table.value;
        uint32_t sum=encode(table[count<3?count:2])^value;
        memory[32]=sum;
        return (uint64_t(encode(Config::epsilon))<<32)|(sum^encode(*address));
#elif LOOP_CASE==12
        float a=cpphdl::hls::math::decode(memory[count]),b=cpphdl::hls::math::decode(value);
        uint32_t comparisons=uint32_t(a<b)|(uint32_t(a>b)<<1)|(uint32_t(a==b)<<2)|
            (uint32_t(a!=b)<<3)|(uint32_t(a<=b)<<4)|(uint32_t(a>=b)<<5);
        memory[32]=comparisons;return comparisons;
#elif LOOP_CASE==13
        auto floats=cpphdl::hls::external_memory<float>(base);
        FourFloats a,b,c;
        for(unsigned lane=0;lane<4;++lane) {
            a.lane[lane]=floats[lane];b.lane[lane]=floats[16+lane];
            c.lane[lane]=cpphdl::hls::math::decode(value);
        }
        auto result=parallel_mac(a,b,c,count);
        for(unsigned lane=0;lane<4;++lane)floats[32+lane]=result.lane[lane];
        return (uint64_t(cpphdl::hls::math::encode(result.lane[3]))<<32)|cpphdl::hls::math::encode(result.lane[0]);
#elif LOOP_CASE==14
        // By-value command parameters are writable C++ locals. Their updates
        // cross the backedge independently of the immutable argument ports.
        while(count) {
            value+=*cpphdl::hls::external_memory<uint32_t>(base);
            base+=4;--count;
        }
        memory[32]=value;return (uint64_t(base)<<32)|value;
#elif LOOP_CASE==15
        // Const accessors can have pointed-to memory effects. Their ordered
        // read/write semantics must survive spatial expansion of the packet.
        LaneMemory input{memory},output{memory};
        uint32_t updated[4];
#pragma omp simd
        for(unsigned lane=0;lane<4;++lane) {
            updated[lane]=input.read(lane)+value;
            output.write(lane,updated[lane]);
        }
        uint32_t sum=0;
        for(unsigned lane=0;lane<4;++lane)sum+=updated[lane];
        memory[32]=sum;return sum;
#elif LOOP_CASE==16
        // A memory-reading loop test cannot be duplicated as a pure test.
        uint32_t sum=value,i=0;
        while(i<count && (memory[i]&1u))sum+=memory[i++];
        memory[32]=sum;return sum;
#elif LOOP_CASE==17
        // Rotating a scalar test must still evaluate its increment exactly
        // once initially and once after each iteration, including continue.
        uint32_t sum=value,i=0;
        while(i++<count) {
            if(i==2)continue;
            sum+=memory[i-1];
        }
        memory[32]=sum;return (uint64_t(i)<<32)|sum;
#elif LOOP_CASE==18
        uint32_t data[8],sum=value;
        for(unsigned i=0;i<8;++i)data[i]=memory[i];
        for(unsigned round=0;round<count;++round) {
            uint32_t lanes[4];
#pragma omp simd
            for(unsigned lane=0;lane<4;++lane)
                lanes[lane]=lane<count?data[(round+lane)&7]+data[(round+lane+1)&7]:value;
            sum+=lanes[0]+lanes[1]+lanes[2]+lanes[3];
        }
        memory[32]=sum;return sum;
#elif LOOP_CASE==19
        uint32_t data[8],sum=value;
        for(unsigned i=0;i<8;++i)data[i]=memory[i];
        for(unsigned round=0;round<count;++round) {
            const auto first=data[round&7];
            const auto second=data[first&7];
            data[(round+1)&7]=second+value;
            sum+=data[(round+1)&7];
        }
        memory[32]=sum;return sum;
#elif LOOP_CASE==20
        uint32_t data[4]{};
        for(unsigned i=0;i<count;++i)data[(i+value)&3]=memory[i]+i;
        uint32_t sum=value;
        for(unsigned i=0;i<4;++i)sum=sum*33+data[i];
        memory[32]=sum;return sum;
#elif LOOP_CASE==21
        uint32_t data[8]{};
        for(unsigned i=0;i<count;++i) {
            auto word=memory[i];data[i]=word;
            if(word&1)break;
        }
        uint32_t sum=value;
        for(unsigned i=0;i<count;++i)sum+=data[i];
        memory[32]=sum;return sum;
#elif LOOP_CASE==22
        struct Packet {uint32_t lane[256];};
        Packet packet{};
        packet.lane[255]=value;packet.lane[0]=count;
        uint32_t sum=packet.lane[255]+packet.lane[0]+packet.lane[128];
        memory[32]=sum;return sum;
#elif LOOP_CASE==23
        uint32_t data[256]{};
        for(unsigned i=0;i<count;++i)data[(i+value)&255]=memory[i];
        uint32_t sum=value;
        for(unsigned i=0;i<256;++i)sum+=data[i];
        memory[32]=sum;return sum;
#else
        uint32_t sum=value,finishedInput;
        if(count&1) {
            for(uint32_t i=0;i<count;++i)sum+=memory[i];
            finishedInput=sum+11;
        } else {sum+=memory[0];finishedInput=sum+23;}
        auto result=finish_sum(finishedInput,value);
        memory[32]=result.lane[0];
        return (uint64_t(result.lane[1])<<32)|result.lane[0];
#endif
    }
};
#if LOOP_CASE==11
using SourceLoop=SourceLoopMethods<FloatConstants>;
#else
using SourceLoop=SourceLoopMethods;
#endif
cpphdl::hls::ClockedMemory<SourceLoop,32,(LOOP_CASE>=6),64,8,true> worker;
#ifndef SYNTHESIS
#include "SourceLoopReference.h"
#include <cstdio>
#include <cstring>
int main() {
    auto memory=loopInitial(),expected=memory;
    cpphdl::hls::bind_external_memory(memory.data(),sizeof(memory),0x1000);
    for(unsigned id=0;id<(LOOP_CASE==12?81:64);++id) {
        auto count=id%9,value=loopValue(id);
        auto read=[&](unsigned index){return expected.at(index);};
        auto write=[&](unsigned index,uint32_t value){expected.at(index)=value;};
        auto result=loopReference(count,value,read,write);
        if(SourceLoop{}.command(0x1000,count,value)!=result || memory!=expected)return 1;
    }
    std::puts("PASS native ordinary loops and dot product");
}
#endif
