#include "../../hls/MemoryMapTypes.h"
#include "../../hls/StridedView.h"
#include "../../hls/math/Maximum.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <random>
struct Add
{
    cpphdl::hls::MapOutput<1> command(cpphdl::hls::MapInput<2> in, cpphdl::hls::MapInput<2>,
                                      cpphdl::hls::MapInput<2>) const
    {
        return {{in.values[0] + in.values[1]}};
    }
};
int main()
{
    using namespace cpphdl::hls;
    MapDescriptor<2, 1> d;
    d.rows = 2;
    d.columns = 3;
    d.read_base[0] = 0;
    d.read_base[1] = 128;
    d.write_base[0] = 256;
    d.read_size[0] = d.read_size[1] = d.write_size[0] = 4;
    d.read_column_stride[0] = 4;
    d.read_column_stride[1] = 0;
    d.write_column_stride[0] = 4;
    d.read_row_stride[0] = d.read_row_stride[1] = d.write_row_stride[0] = 16;
    std::array<uint32_t, 128> ram{};
    for (unsigned i = 0; i < ram.size(); ++i)
        ram[i] = i * 17;
    auto read = [&](uint32_t address, uint32_t size)
    {
        assert(size == 4);
        return ram.at(address / 4);
    };
    auto write = [&](uint32_t address, uint32_t value, uint32_t size)
    {
        assert(size == 4);
        ram.at(address / 4) = value;
    };
    assert(map_runtime(Add{}, d, read, write));
    for (unsigned r = 0; r < 2; ++r)
        for (unsigned c = 0; c < 3; ++c)
            assert(ram[64 + r * 4 + c] == (r * 8 + c + 32) * 17);
    d.write_base[0] = 2;
    unsigned calls = 0;
    assert(!map_runtime(
        Add{}, d,
        [&](uint32_t, uint32_t)
        {
            ++calls;
            return 0u;
        },
        [&](uint32_t, uint32_t, uint32_t) { ++calls; }));
    assert(calls == 0);
    d.write_base[0] = 0;
    d.read_count = 1;
    assert(map_descriptor_valid(d));
    d.write_base[0] = 4;
    assert(!map_descriptor_valid(d));
    d.write_base[0] = 0xfffffffcu;
    assert(!map_descriptor_valid(d));
    d.rows = 0;
    assert(!map_descriptor_valid(d));
    StridedMatrixView v{64, 4, 8, 32, 4, 4};
    assert(v.transpose().address(3, 2) == v.address(2, 3));
    assert(v.sub(1, 2, 2, 3).address(1, 1) == v.address(2, 3));
    assert(StridedViewMethods::view_valid(64, 4, 8, 32, 4, false));
    assert(!StridedViewMethods::view_valid(0xfffffffcu, 2, 8, 32, 4, false));
    assert(StridedViewMethods::view_next_inner(60, 0, 8, 4, false) == 1);
    assert(StridedViewMethods::view_next_inner(64, 0, 8, 0, false) == 8);
    assert(StridedViewMethods::view_next_inner(64, 0, 8, 16, false) == 1);
    std::mt19937 rng(17);
    math::MaximumMethods maximum;
    for (unsigned trial = 0; trial < 1024; ++trial)
    {
        std::array<uint32_t, 8> bits{};
        std::array<uint64_t, 8> values{};
        for (unsigned i = 0; i < 8; ++i)
        {
            bits[i] = rng();
            values[i] = (uint64_t(i) << 32) | bits[i];
        }
        if (trial % 4 == 0)
        {
            bits[0] = 0x7fc00001;
            values[0] = bits[0];
        }
        if (trial % 4 == 1)
        {
            bits[0] = 0x80000000;
            bits[1] = 0;
            values[0] = bits[0];
            values[1] = uint64_t(1) << 32;
        }
        float best;
        std::memcpy(&best, &bits[0], 4);
        unsigned index = 0;
        for (unsigned i = 1; i < 8; ++i)
        {
            float next;
            std::memcpy(&next, &bits[i], 4);
            if (best < next)
            {
                best = next;
                index = i;
            }
        }
        std::shuffle(values.begin(), values.end(), rng);
        uint64_t actual = math::maximum_empty;
        for (auto value : values)
            actual = maximum.maximum_merge(actual, value);
        assert(actual == ((uint64_t(index) << 32) | bits[index]));
    }
    std::puts("PASS map descriptors, affine views and FP32 first-maximum reductions");
}
