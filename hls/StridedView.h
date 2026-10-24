#pragma once
#include <cstdint>

namespace cpphdl::hls
{
// Byte-addressed affine view. Pointers in the ordinary C++ MatrixView use the
// same two strides in elements; neither transpose nor subview copies data.
struct StridedMatrixView
{
    uint32_t base = 0, rows = 0, columns = 0, row_stride = 0, column_stride = 0, element_bytes = 4;
    uint32_t address(uint32_t row, uint32_t column) const
    {
        return base + row * row_stride + column * column_stride;
    }
    StridedMatrixView transpose() const
    {
        return {base, columns, rows, column_stride, row_stride, element_bytes};
    }
    StridedMatrixView sub(uint32_t row, uint32_t column, uint32_t m, uint32_t n) const
    {
        return {address(row, column), m, n, row_stride, column_stride, element_bytes};
    }
};
// Shared CPU/RTL beat traversal. Select the shorter stride as the inner
// dimension. Contiguous elements in a 64-byte beat share one request; sparse
// elements remain correct without a runtime divider or an unbounded gather.
struct StridedViewMethods
{
    static uint32_t view_next_inner(uint32_t address, uint32_t inner, uint32_t limit,
                                    uint32_t stride, bool half)
    {
        uint32_t step = 1;
        if (stride == 0)
            step = limit - inner;
        else if (stride == (half ? 2u : 4u))
            step = (64 - (address & 63)) >> (half ? 1 : 2);
        return step < limit - inner ? inner + step : limit;
    }
    static bool view_valid(uint32_t base, uint32_t rows, uint32_t columns, uint32_t row_stride,
                           uint32_t column_stride, bool half)
    {
        return rows && columns && ((base | row_stride | column_stride) & (half ? 1u : 3u)) == 0 &&
               uint64_t(base) + uint64_t(rows - 1) * row_stride +
                       uint64_t(columns - 1) * column_stride + (half ? 2 : 4) <=
                   0x100000000ull;
    }
};
} // namespace cpphdl::hls
