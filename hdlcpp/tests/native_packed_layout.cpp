#include "generated/native_packed_layout.h"
#include <cassert>
#include <cstdio>

long _system_clock = 0;
using namespace native_packed_layout;

struct Custom {
    cpphdl::logic<68> pack() const { return cpphdl::logic<68>(0xaabbccdd); }
};

struct Derived : First {
    using First::operator=;
    cpphdl::logic<68> pack() const { return cpphdl::logic<68>(0x12345678); }
};

static_assert(First::__hdlcpp_layout_compatible<Second>());
static_assert(!First::__hdlcpp_layout_compatible<Reordered>());
static_assert(!First::__hdlcpp_layout_compatible<Overlay>());
static_assert(!First::__hdlcpp_layout_compatible<Derived>());
static_assert(!cpphdl::detail::compatible_generated_layout<Derived, First>::value);
static_assert(!cpphdl::detail::native_packed_element<Derived>::value);
static_assert(!cpphdl::detail::native_packed_element<Overlay>::value);
static_assert(!cpphdl::detail::native_packed_element<Custom>::value);
static_assert(OuterFirst::__hdlcpp_layout_compatible<OuterSecond>());

int main()
{
    using Row = cpphdl::array<3, First, true>;
    using Matrix = cpphdl::array<2, Row, true>;
    static_assert(cpphdl::type_width<decltype(std::declval<Matrix&>().bits(105, 62))>() == 408);
    cpphdl::array<3, Words, true> defaultWords{};
    assert(defaultWords.pack() == cpphdl::logic<123>{});
    for (size_t selected = 0; selected < 408; ++selected) {
        cpphdl::logic<408> sparse{};
        sparse.set(selected, true);
        const Matrix matrix = sparse;
        assert(matrix.pack() == sparse);
    }
    for (unsigned pattern = 0; pattern < 2048; ++pattern) {
        cpphdl::logic<408> input{};
        for (size_t bit = 0; bit < 408; ++bit)
            input.set(bit, ((bit * 17 + pattern * 7) % 31) < 13);
        Matrix matrix = input;
        const Matrix& view = matrix;
        for (size_t row = 0; row < 2; ++row) {
            const Row inner = view[row];
            for (size_t index = 0; index < 3; ++index) {
                const First value = inner[index];
                const auto packed = value.pack();
                for (size_t bit = 0; bit < 68; ++bit)
                    assert(packed.get(bit) == input.get((row * 3 + index) * 68 + bit));
            }
        }
        assert(matrix.pack() == input);
        const auto source = cpphdl::unpack_value<OuterFirst>(cpphdl::logic<73>(input));
        const auto converted = cpphdl::convert_packed<OuterSecond>(source);
        assert(converted.pack() == source.pack());
        OuterSecond assigned;
        assigned = source;
        assert(assigned.pack() == source.pack());
        assert(cpphdl::convert_packed<Reordered>(source.entry).pack() == source.entry.pack());
        assert(cpphdl::convert_packed<First>(cpphdl::logic<68>(input)).pack() == cpphdl::logic<68>(input));

        First replacement{};
        replacement.data = uint64_t(pattern) << 37;
        replacement.data.set(64, pattern & 1);
        replacement.tag = pattern;
        Row inner = view[1];
        inner[1] = replacement;
        matrix[1] = inner;
        input.bits(339, 272) = replacement.pack();
        assert(matrix.pack() == input);
        matrix.bits(105, 62) = cpphdl::logic<44>(0xafeef7c929ull);
        input.bits(105, 62) = cpphdl::logic<44>(0xafeef7c929ull);
        assert(matrix.pack() == input);
        const auto snapshot = cpphdl::logic<408>(input.bits(105, 62));
        matrix.bits(130, 87) = matrix.bits(105, 62);
        input.bits(130, 87) = snapshot;
        assert(matrix.pack() == input);
#ifdef CPPHDL_NATIVE_PACKED
        const Matrix other = cpphdl::logic<408>(pattern);
        assert(cpphdl::pack_value<408>(matrix & other) == (input & other.pack()));
        assert(cpphdl::pack_value<408>(matrix | other) == (input | other.pack()));
        assert(cpphdl::pack_value<408>(matrix ^ other) == (input ^ other.pack()));
        assert(cpphdl::pack_value<408>(~matrix) == ~input);
        matrix = matrix << 3;
        input = input << 3;
        assert(matrix.pack() == input);
#endif
        matrix = cpphdl::logic<408>{};
        assert(matrix.pack() == cpphdl::logic<408>{});
        matrix = input;
        matrix = 0;
        assert(matrix.pack() == cpphdl::logic<408>{});
    }
    assert(cpphdl::convert_packed<First>(Custom{}).pack() == Custom{}.pack());
    assert(cpphdl::convert_packed<First>(Derived{}).pack() == Derived{}.pack());
    First assigned;
    assigned = Derived{};
    assert(assigned.pack() == Derived{}.pack());
    std::puts("native packed layout: 408 sparse bits and 2048 nested patterns pass");
}
