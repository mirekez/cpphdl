#include "../../Clocked.h"
#include "../../math/Float.h"
using cpphdl::hls::math::decode;
using cpphdl::hls::math::encode;
template<class T> T square(T x) { return x*x; }
inline float adjust(float x) {
    for(int i=0;i<2;++i) x+=0.125f;
    return x;
}
struct PipelineMethods {
    uint64_t command(uint32_t operation,uint32_t index,uint32_t value) const {
        // Limited significand width keeps host OpenMP's unspecified reduction
        // order exact; separate tests cover FP32 rounding-sensitive cases.
        float x=decode((value&0x007f8000u)|0x3f000000u);
        float scale=0.5f,scratch=0.0f,sum=0.125f;
        float old[2][3]={{x,x,x},{x,x,x}},next[2][3]{};
#pragma omp parallel for collapse(2) private(scratch) firstprivate(scale)
        for(int row=0;row<2;++row)
            for(int col=0;col<3;++col) {
                scratch=square(x)*scale;
                next[row][col]=adjust(scratch)+(col>0 ? old[row][col-1] : 0.0f);
            }
#pragma omp parallel for collapse(2) reduction(+:sum)
        for(int row=0;row<2;++row)
            for(int col=0;col<3;++col) sum+=next[row][col];
        float product=0.25f;
#pragma omp parallel for reduction(*:product)
        for(int i=0;i<3;++i) product*=((operation&1)?0.5f:1.0f);
#pragma omp simd reduction(+:sum)
        for(int i=0;i<0;++i) sum+=x;
        return (uint64_t(index)<<32)|encode(sum*product);
    }
    uint32_t expected(uint32_t operation,uint32_t,uint32_t value) const {
        float x=decode((value&0x007f8000u)|0x3f000000u),sum=0.125f;
        for(unsigned row=0;row<2;++row)for(unsigned col=0;col<3;++col)
            sum+=x*x*0.5f+0.25f+(col?x:0.0f);
        float factor=0.25f;
        for(unsigned i=0;i<3;++i) factor*=operation&1?0.5f:1.0f;
        return encode(sum*factor);
    }
};
#include "Test.h"
