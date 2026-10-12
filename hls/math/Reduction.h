#pragma once
#include "Float.h"
namespace cpphdl::hls::math {
template<class T,unsigned Rows,unsigned Cols> struct MatrixValues {
    T value[Rows][Cols];
    uint32_t tag;
};
template<class T,unsigned Rows> struct VectorValues {
    T value[Rows];
    uint32_t tag;
};
// CPU SIMD reduction and HLS balanced FP32 reduction. Floating OpenMP does not
// specify a single association order: exact checks require exact domains;
// general inputs need an explicit tree reference or a numerical tolerance.
template<unsigned Rows,unsigned Cols>
VectorValues<float,Rows> reduce_rows(MatrixValues<float,Rows,Cols> input) {
    VectorValues<float,Rows> result{};result.tag=input.tag;
    for(int row=0;row<Rows;++row) {
        float sum=0.0f;
#pragma omp simd reduction(+:sum)
        for(int col=0;col<Cols;++col) sum+=input.value[row][col];
        result.value[row]=sum;
    }
    return result;
}
// Explicit pairwise tree: the same association on CPU and HLS. Odd inputs
// carry into the next level; a final +0 supplies the reduction identity.
template<unsigned Rows,unsigned Cols>
VectorValues<float,Rows> reduce_rows_tree(MatrixValues<float,Rows,Cols> input) {
    static_assert(Cols>0,"tree requires at least one input");
    if constexpr(Cols==1) {
        VectorValues<float,Rows> result{};result.tag=input.tag;
        for(int row=0;row<Rows;++row)result.value[row]=0.0f+input.value[row][0];
        return result;
    }else {
        MatrixValues<float,Rows,(Cols+1)/2> next{};next.tag=input.tag;
#pragma omp simd collapse(2)
        for(int row=0;row<Rows;++row)for(int col=0;col<(Cols+1)/2;++col)
            next.value[row][col]=2*col+1<Cols?input.value[row][2*col]+input.value[row][2*col+1]:input.value[row][2*col];
        return reduce_rows_tree<Rows,(Cols+1)/2>(next);
    }
}
template<unsigned Rows,unsigned Cols> struct ReductionMethods {
    VectorValues<uint32_t,Rows> command(MatrixValues<uint32_t,Rows,Cols> input,
        MatrixValues<uint32_t,Rows,Cols>,MatrixValues<uint32_t,Rows,Cols>) const {
        MatrixValues<float,Rows,Cols> decoded{};decoded.tag=input.tag;
        for(int row=0;row<Rows;++row)for(int col=0;col<Cols;++col) decoded.value[row][col]=decode(input.value[row][col]);
        auto sums=reduce_rows_tree<Rows,Cols>(decoded);
        VectorValues<uint32_t,Rows> result{};result.tag=sums.tag;
        for(int row=0;row<Rows;++row) result.value[row]=encode(sums.value[row]);
        return result;
    }
};
}
