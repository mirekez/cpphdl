#pragma once
#include <cstdint>
#include <type_traits>

namespace cpphdl::hls
{
// Plain value packets use the same ClockedPipeline lowering as other C++
// arithmetic policies. Representation bits cross the hardware boundary.
template <unsigned Reads> struct MapInput
{
    uint32_t values[Reads];
    uint32_t operation, parameter, index;
};
template <unsigned Writes> struct MapOutput
{
    uint32_t values[Writes];
};
template <unsigned Reads, unsigned Writes> struct MapDescriptor
{
    uint32_t rows = 0, columns = 0, read_count = Reads, write_count = Writes;
    uint32_t operation = 0, parameter = 0;
    uint32_t read_base[Reads]{}, read_row_stride[Reads]{}, read_column_stride[Reads]{},
        read_size[Reads]{};
    uint32_t write_base[Writes]{}, write_row_stride[Writes]{}, write_column_stride[Writes]{},
        write_size[Writes]{};
};

// Conservative proof used by the host reference and mirrored by the hardware
// admission logic. Exact per-iteration in-place aliases and separated row
// regions are accepted; any unproved cross-iteration dependence is rejected.
template <unsigned Reads, unsigned Writes>
bool map_descriptor_valid(const MapDescriptor<Reads, Writes> &d)
{
    if (!d.rows || !d.columns || uint64_t(d.rows) * d.columns > 0xffffffffull ||
        d.read_count > Reads || !d.write_count || d.write_count > Writes)
        return false;
    auto end = [&](uint32_t base, uint32_t row, uint32_t column, uint32_t size)
    {
        return uint64_t(base) + uint64_t(d.rows - 1) * row + uint64_t(d.columns - 1) * column +
               size;
    };
    auto valid = [&](uint32_t base, uint32_t row, uint32_t column, uint32_t size, bool write)
    {
        return (size == 1 || size == 2 || size == 4) && ((base | row | column) & (size - 1)) == 0 &&
               end(base, row, column, size) <= 0x100000000ull &&
               (!write ||
                ((d.columns == 1 || column >= size) &&
                 (d.rows == 1 || uint64_t(row) >= uint64_t(d.columns - 1) * column + size)));
    };
    auto disjoint = [&](uint32_t a, uint32_t ar, uint32_t ac, uint32_t az, uint32_t b, uint32_t br,
                        uint32_t bc, uint32_t bz)
    {
        uint64_t aspan = uint64_t(d.columns - 1) * ac + az,
                 bspan = uint64_t(d.columns - 1) * bc + bz;
        uint64_t delta = a <= b ? uint64_t(b) - a : uint64_t(a) - b;
        bool periodic = ar && ar == br && aspan <= ar && bspan <= ar &&
                        (a <= b ? (delta >= aspan && delta + bspan <= ar)
                                : (delta >= bspan && delta + aspan <= ar));
        return end(a, ar, ac, az) <= b || end(b, br, bc, bz) <= a || periodic;
    };
    for (unsigned r = 0; r < d.read_count; ++r)
        if (!valid(d.read_base[r], d.read_row_stride[r], d.read_column_stride[r], d.read_size[r],
                   false))
            return false;
    for (unsigned w = 0; w < d.write_count; ++w)
    {
        if (!valid(d.write_base[w], d.write_row_stride[w], d.write_column_stride[w],
                   d.write_size[w], true))
            return false;
        for (unsigned v = 0; v < w; ++v)
            if (!disjoint(d.write_base[w], d.write_row_stride[w], d.write_column_stride[w],
                          d.write_size[w], d.write_base[v], d.write_row_stride[v],
                          d.write_column_stride[v], d.write_size[v]))
                return false;
        for (unsigned r = 0; r < d.read_count; ++r)
        {
            bool same = d.read_base[r] == d.write_base[w] && d.read_size[r] == d.write_size[w] &&
                        (d.rows == 1 || d.read_row_stride[r] == d.write_row_stride[w]) &&
                        (d.columns == 1 || d.read_column_stride[r] == d.write_column_stride[w]);
            if (!same && !disjoint(d.read_base[r], d.read_row_stride[r], d.read_column_stride[r],
                                   d.read_size[r], d.write_base[w], d.write_row_stride[w],
                                   d.write_column_stride[w], d.write_size[w]))
                return false;
        }
    }
    return true;
}

// Ordinary C++ OpenMP reference for an independent affine map. The caller
// supplies byte-addressed read(address,size) and write(address,bits,size).
// Inputs are captured before the iteration's writes. As with any OpenMP map,
// callers must ensure there are no cross-iteration memory dependences.
// Invalid/empty descriptors return false before invoking either callback.
template <class Methods, unsigned Reads, unsigned Writes, class Read, class Write>
bool map_runtime(Methods methods, const MapDescriptor<Reads, Writes> &descriptor, Read read,
                 Write write)
{
    static_assert(std::is_empty<Methods>::value,
                  "map policies must be stateless; pass context in the packet");
    if (!map_descriptor_valid(descriptor))
        return false;
#pragma omp parallel for collapse(2)
    for (uint32_t row = 0; row < descriptor.rows; ++row)
        for (uint32_t column = 0; column < descriptor.columns; ++column)
        {
            MapInput<Reads> input{}, unused{};
            input.operation = descriptor.operation;
            input.parameter = descriptor.parameter;
            input.index = row * descriptor.columns + column;
            for (unsigned stream = 0; stream < descriptor.read_count; ++stream)
                input.values[stream] =
                    read(descriptor.read_base[stream] + row * descriptor.read_row_stride[stream] +
                             column * descriptor.read_column_stride[stream],
                         descriptor.read_size[stream]);
            MapOutput<Writes> output = methods.command(input, unused, unused);
            for (unsigned stream = 0; stream < descriptor.write_count; ++stream)
                write(descriptor.write_base[stream] + row * descriptor.write_row_stride[stream] +
                          column * descriptor.write_column_stride[stream],
                      output.values[stream], descriptor.write_size[stream]);
        }
    return true;
}
} // namespace cpphdl::hls
