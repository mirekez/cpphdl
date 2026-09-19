#pragma once

#include <cxxrtl/cxxrtl.h>

namespace cpphdl::netlist {

template<size_t Offset, size_t DestinationBits, class High, class Low>
CXXRTL_ALWAYS_INLINE
void append_words(cxxrtl::value<DestinationBits>& destination,
                  const cxxrtl::concat_expr<High, Low>& source);

template<size_t Offset, size_t DestinationBits, class Source>
CXXRTL_ALWAYS_INLINE
void append_words(cxxrtl::value<DestinationBits>& destination, const Source& source)
{
    const cxxrtl::value<Source::bits>& materialized = source;
    constexpr size_t chunk_bits = cxxrtl::chunk_traits<cxxrtl::chunk_t>::bits;
    constexpr size_t start = Offset / chunk_bits;
    constexpr size_t shift = Offset % chunk_bits;
    for (size_t index = 0; index < materialized.chunks; ++index) {
        auto word = materialized.data[index];
        if (index + 1 == materialized.chunks) {
            word &= materialized.msb_mask;
        }
        destination.data[start + index] |= word << shift;
        if constexpr (shift != 0) {
            if (start + index + 1 < destination.chunks) {
                destination.data[start + index + 1] |= word >> (chunk_bits - shift);
            }
        }
    }
}

template<size_t Offset, size_t DestinationBits, class High, class Low>
CXXRTL_ALWAYS_INLINE
void append_words(cxxrtl::value<DestinationBits>& destination,
                  const cxxrtl::concat_expr<High, Low>& source)
{
    append_words<Offset>(destination, source.ls_expr);
    append_words<Offset + Low::bits>(destination, source.ms_expr);
}

template<class High, class Low>
CXXRTL_ALWAYS_INLINE
cxxrtl::value<High::bits + Low::bits> assemble(const cxxrtl::concat_expr<High, Low>& source)
{
    // Connectivity is a mapping, not a chain of growing packed temporaries.
    // Assemble normalized two-state leaves directly into one destination.
    cxxrtl::value<High::bits + Low::bits> destination;
    append_words<0>(destination, source);
    return destination;
}

}
