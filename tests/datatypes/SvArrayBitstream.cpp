#include "cpphdl.h"
#include <cstdio>

long _system_clock = 0;

struct Entry
{
    cpphdl::logic<5> flags;
    cpphdl::logic<6> tag;

    static constexpr size_t _size_bits() { return 11; }

    template<size_t Width>
    Entry& operator=(const cpphdl::logic<Width>& value)
    {
        tag = uint64_t(value);
        flags = uint64_t(value) >> 6;
        return *this;
    }

    cpphdl::logic<11> pack() const
    {
        return cpphdl::logic<11>((uint64_t(flags) << 6) | uint64_t(tag));
    }
};

using Row = cpphdl::array<4, Entry>;
using Matrix = cpphdl::array<2, Row>;

static_assert(cpphdl::type_width<Row>() == 44);
static_assert(cpphdl::type_width<Matrix>() == 88);
static_assert(cpphdl::type_width<cpphdl::reg<Matrix>>() == 88);

int main()
{
    for (size_t bit = 0; bit < 88; ++bit) {
        cpphdl::logic<88> source = 0;
        source.set(bit, 1);
        const Matrix matrix = cpphdl::unpack_value<Matrix>(source);
        cpphdl::reg<Matrix> registered;
        registered._next = matrix;
        registered.strobe();
        if (cpphdl::pack_value<88>(matrix) != source ||
            cpphdl::pack_value<88>(registered) != source ||
            cpphdl::pack_value<44>(matrix) != cpphdl::logic<44>(source)) {
            std::fprintf(stderr, "array bitstream mismatch at bit %zu\n", bit);
            return 1;
        }
        for (size_t entry = 0; entry < 8; ++entry) {
            const uint64_t expected = entry == bit / 11 ? uint64_t{1} << (bit % 11) : 0;
            if (uint64_t(matrix[entry / 4][entry % 4].pack()) != expected) return 2;
        }
    }
    const auto extended = cpphdl::unpack_value<Matrix>(cpphdl::logic<1>(1));
    if (cpphdl::pack_value<88>(extended) != cpphdl::logic<88>(1)) return 3;
}
