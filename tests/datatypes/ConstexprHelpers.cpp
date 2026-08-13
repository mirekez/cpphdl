#include "cpphdl.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>

using namespace cpphdl;

// Constexpr/runtime helper requirements tested here:
// 1. Empty and ordinary concatenations remain valid constant expressions.
// 2. Width-limited logic complement preserves the declared width.
// 3. sv_bits and packed aggregate operations preserve bits above bit 63.
// 4. Packed aggregate byteswap uses pack() field order.
// 5. Packed/unpacked array initializer lists preserve order and zero-fill.
// 6. Packed array proxies expose their stored value type to generic code.
// 7. Packed-aggregate shifts do not intercept scalar-like cpphdl registers.
// 8. Fixed-width slices, optimized concatenations, and wide packing preserve bit order.
// 9. Packed arrays accept complete repeated bit vectors during constant evaluation.

struct ConstexprWidePacked
{
    logic<96> value;

    constexpr static size_t _size_bits() { return 96; }
    logic<96> pack() const { return value; }
};

constexpr logic<8> constexpr_input(0x0f);
constexpr logic<8> constexpr_inverse = ~constexpr_input;
constexpr logic<8> constexpr_and = constexpr_input & logic<8>(0x33);
constexpr logic<8> constexpr_or = constexpr_input | logic<8>(0x30);
constexpr logic<8> constexpr_xor = constexpr_input ^ logic<8>(0x3c);
constexpr auto constexpr_cat = cat(logic<4>(0xa), logic<4>(0x5));
constexpr logic<8> constexpr_cat_logic = constexpr_cat;
constexpr auto constexpr_bit_cat = cat(logic<1>(1), logic<1>(0), logic<1>(1));
constexpr auto constexpr_byte_cat = cat(logic<8>(0x12), logic<16>(0x3456));

static_assert(SUM<>() == 0, "empty concatenation width must be zero");
static_assert((uint64_t)constexpr_inverse == 0xf0, "logic complement must retain width");
static_assert((uint64_t)constexpr_and == 0x03, "logic conjunction must be constexpr");
static_assert((uint64_t)constexpr_or == 0x3f, "logic disjunction must be constexpr");
static_assert((uint64_t)constexpr_xor == 0x33, "logic exclusive-or must be constexpr");
static_assert((uint64_t)constexpr_cat == 0xa5, "concatenation must be constexpr");
static_assert((uint64_t)constexpr_cat_logic == 0xa5, "cat-to-logic conversion must be constexpr");
static_assert((uint64_t)constexpr_bit_cat == 0x5, "one-bit concatenation order must be preserved");
static_assert((uint64_t)constexpr_byte_cat == 0x123456, "byte-aligned concatenation order must be preserved");
static_assert((uint64_t)(constexpr_cat + 1) == 0xa6, "cat arithmetic must be unambiguous");

constexpr array<4, logic<4>, true> constexpr_repeated_array = [] {
    array<4, logic<4>, true> value{};
    value = repeat<4, 4>(logic<4>(3));
    return value;
}();
static_assert((uint64_t)constexpr_repeated_array.data == 0x3333,
    "packed whole-value assignment must remain constexpr");
static_assert((uint64_t)std::as_const(constexpr_repeated_array)[0] == 3,
    "const packed element reads must remain constexpr");

static bool expect(bool condition, const char* message)
{
    if (!condition) std::printf("%s\n", message);
    return condition;
}

int main()
{
    bool ok = true;

    logic<160> source = 0;
    source[100] = 1;
    source[63] = 1;
    source[70] = 1;
    logic<9> fixed_slice = source.slice<71, 63>();
    ok &= expect((uint64_t)fixed_slice == 0x81,
        "fixed-width slice did not preserve bits across a byte boundary");
    logic<96> slice = sv_bits<96>(source, 127, 32);
    ok &= expect((bool)slice[68], "sv_bits truncated a selected bit above bit 63");

    auto packed_wide_cat = cat(logic<5>(8), logic<64>(0x10000), logic<13>(0));
    logic<82> packed_cat = pack_value<82>(packed_wide_cat);
    ok &= expect((bool)packed_cat[80], "pack_value truncated a wide concatenation above bit 63");

    ConstexprWidePacked packed{};
    packed.value[70] = 1;
    logic<96> shifted = packed << 1;
    ok &= expect((bool)shifted[71], "packed aggregate shift truncated a bit above bit 63");
    logic<32> packed_slice = sv_bits<32>(packed, 79, 48);
    ok &= expect((bool)packed_slice[22], "sv_bits did not use a packed aggregate's pack() value");

    logic<103> cat_high = 0;
    cat_high[0] = 1;
    cat_high[64] = 1;
    cat_high[102] = 1;
    logic<11> cat_middle(0x5a3);
    logic<2> cat_low(2);
    logic<116> wide_cat = cat(cat_high, cat_middle, cat_low);
    for (size_t bit = 0; bit < 103; ++bit) {
        ok &= expect(wide_cat.get(bit + 13) == cat_high.get(bit),
            "non-byte-aligned concatenation corrupted its high field");
    }
    for (size_t bit = 0; bit < 11; ++bit) {
        ok &= expect(wide_cat.get(bit + 2) == cat_middle.get(bit),
            "non-byte-aligned concatenation corrupted its middle field");
    }
    for (size_t bit = 0; bit < 2; ++bit) {
        ok &= expect(wide_cat.get(bit) == cat_low.get(bit),
            "non-byte-aligned concatenation corrupted its low field");
    }
    ok &= expect((wide_cat.bytes[logic<116>::SIZE - 1] & 0xf0u) == 0,
        "non-byte-aligned concatenation left nonzero padding bits");

    packed.value = 0;
    for (size_t byte = 0; byte < 12; ++byte) {
        packed.value.bits(byte * 8 + 7, byte * 8) = byte;
    }
    logic<96> swapped = byteswap(packed);
    for (size_t byte = 0; byte < 12; ++byte) {
        ok &= expect((uint64_t)swapped.bits(byte * 8 + 7, byte * 8) == 11 - byte,
            "packed aggregate byteswap used the wrong byte order");
    }

    array<4, u8, false> unpacked_array = {u8(1), u8(2)};
    ok &= expect((uint64_t)unpacked_array[0] == 1 &&
        (uint64_t)unpacked_array[1] == 2 &&
        (uint64_t)unpacked_array[2] == 0 &&
        (uint64_t)unpacked_array[3] == 0,
        "unpacked initializer list did not preserve order and zero-fill");
    ok &= expect((uint64_t)unpacked_array.pack() == 0x0201,
        "one-byte unpacked array pack used the wrong byte order");

    array<4, logic<4>, true> packed_array = {logic<4>(1), logic<4>(2)};
    ok &= expect((uint64_t)packed_array.pack() == 0x21,
        "packed initializer list did not preserve element order and zero-fill");
    static_assert(std::is_same_v<value_type_for_ref_t<decltype(packed_array[0])>, logic<4>>,
        "packed proxy must expose its stored element type");
    // A second packed selection returns an addressable logic_bits proxy.
    // Value-oriented generated code must still receive a constructible logic value,
    // otherwise casts instantiate the proxy's deleted default and copy constructors.
    static_assert(std::is_same_v<value_type_for_ref_t<logic_bits<1>>, logic<1>>,
        "packed bit proxy must expose an ordinary logic value");

    ok &= expect(!sv_isunknown(packed_array),
        "two-state CppHDL values must never report unknown bits");

    reg<u<8>> scalar_reg;
    scalar_reg.set(u<8>(0x12));
    ok &= expect((uint64_t)(scalar_reg << 1) == 0x24,
        "packed aggregate shift overload intercepted a scalar register");
    return ok ? 0 : 1;
}
