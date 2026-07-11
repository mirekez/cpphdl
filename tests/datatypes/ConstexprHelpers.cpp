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

struct ConstexprWidePacked
{
    logic<96> value;

    constexpr static size_t _size_bits() { return 96; }
    logic<96> pack() const { return value; }
};

constexpr logic<8> constexpr_input(0x0f);
constexpr logic<8> constexpr_inverse = ~constexpr_input;
constexpr auto constexpr_cat = cat(logic<4>(0xa), logic<4>(0x5));

static_assert(SUM<>() == 0, "empty concatenation width must be zero");
static_assert((uint64_t)constexpr_inverse == 0xf0, "logic complement must retain width");
static_assert((uint64_t)constexpr_cat == 0xa5, "concatenation must be constexpr");
static_assert((uint64_t)(constexpr_cat + 1) == 0xa6, "cat arithmetic must be unambiguous");

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
    logic<96> slice = sv_bits<96>(source, 127, 32);
    ok &= expect((bool)slice[68], "sv_bits truncated a selected bit above bit 63");

    ConstexprWidePacked packed{};
    packed.value[70] = 1;
    logic<96> shifted = packed << 1;
    ok &= expect((bool)shifted[71], "packed aggregate shift truncated a bit above bit 63");
    logic<32> packed_slice = sv_bits<32>(packed, 79, 48);
    ok &= expect((bool)packed_slice[22], "sv_bits did not use a packed aggregate's pack() value");

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

    array<4, logic<4>, true> packed_array = {logic<4>(1), logic<4>(2)};
    ok &= expect((uint64_t)packed_array.pack() == 0x21,
        "packed initializer list did not preserve element order and zero-fill");
    static_assert(std::is_same_v<value_type_for_ref_t<decltype(packed_array[0])>, logic<4>>,
        "packed proxy must expose its stored element type");

    ok &= expect(!sv_isunknown(packed_array),
        "two-state CppHDL values must never report unknown bits");

    reg<u<8>> scalar_reg;
    scalar_reg.set(u<8>(0x12));
    ok &= expect((uint64_t)(scalar_reg << 1) == 0x24,
        "packed aggregate shift overload intercepted a scalar register");
    return ok ? 0 : 1;
}
