#include "cpphdl.h"
#include <type_traits>

struct Element {
    using __hdlcpp_layout_owner = Element;
    static constexpr bool __hdlcpp_native_fields = true;
    cpphdl::logic<8> value;
    static constexpr size_t _size_bits() { return 8; }
    Element() = default;
    Element(uint64_t input) : value(input) {}
    cpphdl::logic<8> pack() const { return value; }
};

int main()
{
    using Array = cpphdl::array<2, Element, true>;
#if __cplusplus >= 202002L
    static_assert(std::is_same_v<decltype(std::declval<Array&>()[0]), Element&>);
#else
    static_assert(!std::is_reference_v<decltype(std::declval<Array&>()[0])>);
#endif
    Array values;
    values[0] = Element(0x39);
    values[1] = Element(0xa7);
    const Array& view = values;
    const Element first = view[0];
    const Element second = view[1];
    return uint64_t(first.value) != 0x39 || uint64_t(second.value) != 0xa7 ||
           uint64_t(values.pack()) != 0xa739;
}
