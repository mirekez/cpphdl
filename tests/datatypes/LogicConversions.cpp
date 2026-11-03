#include "cpphdl.h"
#include <cstdio>
#include <type_traits>

long _system_clock = 0;

enum class SignedValue : int { negative = -3 };
struct PackedValue {
    uint64_t value;
    constexpr cpphdl::logic<13> pack() const { return cpphdl::logic<13>(value); }
};
struct RawValue { unsigned char bytes[2]; };

constexpr cpphdl::logic<13> enum_constant(SignedValue::negative);
constexpr cpphdl::logic<24> packed_constant(PackedValue{0x1234});
static_assert(uint64_t(enum_constant) == 0x1ffd);
static_assert(uint64_t(packed_constant) == 0x1234);

// Run the same construction/assignment oracle in C++17 and C++23: constraints
// must select the same scalar, packed, width-conversion and proxy overloads.
bool check_input_overloads()
{
    cpphdl::logic<13> scalar{};
    scalar = SignedValue::negative;
    if (uint64_t(scalar) != 0x1ffd) return false;
    RawValue raw{{0x34, 0x12}};
    cpphdl::logic<16> raw_constructed(raw), raw_assigned{};
    raw_assigned = raw;
    if (raw_constructed.bytes[0] != 0x34 || raw_constructed.bytes[1] != 0x12 ||
        raw_assigned.bytes[0] != 0x34 || raw_assigned.bytes[1] != 0x12) return false;
    for (uint64_t sample = 0; sample < 1024; ++sample) {
        PackedValue source{sample * 7919};
        const auto expected = source.value & 0x1fff;
        cpphdl::logic<13> constructed(source), assigned{};
        assigned = source;
        cpphdl::logic<24> widened(constructed), copied{};
        copied = assigned;
        if (uint64_t(constructed) != expected || uint64_t(assigned) != expected ||
            uint64_t(widened) != expected || uint64_t(copied) != expected) return false;
        cpphdl::logic<32> parent(0xa55aa55a);
        parent.bits(19, 7) = source;
        const uint64_t merged = (0xa55aa55aULL & ~(0x1fffULL << 7)) | (expected << 7);
        cpphdl::logic<16> slice(parent.bits(19, 7));
        if (uint64_t(parent) != merged || uint64_t(slice) != expected) return false;
    }
    return true;
}

template<unsigned Width>
bool check()
{
    using Word = cpphdl::logic<Width>;
    static_assert(std::is_standard_layout_v<Word>);
    static_assert(std::is_trivially_copyable_v<Word>);
    static_assert(sizeof(Word) == (Width + 7) / 8);
    static_assert(alignof(Word) == 1);
    static_assert(std::is_convertible_v<Word, unsigned char>);
    static_assert(std::is_convertible_v<Word, signed char>);
    static_assert(std::is_convertible_v<Word, unsigned short>);
    static_assert(std::is_convertible_v<Word, signed short>);
    static_assert(std::is_convertible_v<Word, unsigned int>);
    static_assert(std::is_convertible_v<Word, signed int>);
    static_assert(std::is_convertible_v<Word, unsigned long>);
    static_assert(std::is_convertible_v<Word, signed long>);
    constexpr Word constant(0x75);
    static_assert(static_cast<unsigned char>(constant) == (Width == 1 ? 1 : 0x75));
    uint64_t random = 23;
    for (unsigned sample = 0; sample < 1000; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        Word word(random);
        uint64_t value = random;
        if constexpr (Width < 64) value &= (uint64_t(1) << Width) - 1;
        unsigned char byte = word;
        signed char signedByte = word;
        unsigned short half = word;
        signed short signedHalf = word;
        unsigned int integer = word;
        signed int signedInteger = word;
        signed long signedLong = word;
        if (byte != static_cast<unsigned char>(value) || signedByte != static_cast<signed char>(value) ||
            half != static_cast<unsigned short>(value) || signedHalf != static_cast<signed short>(value) ||
            integer != static_cast<unsigned int>(value) || signedInteger != static_cast<signed int>(value) ||
            signedLong != static_cast<signed long>(value) || bool(word) != bool(value)) return false;
        if constexpr (Width != 32 && Width != 64) {
            if ((uint64_t(3) - word) != (uint64_t(3) - value) ||
                (uint64_t(3) + word) != (uint64_t(3) + value) ||
                (uint64_t(3) ^ word) != (uint64_t(3) ^ value) ||
                (3 < word) != (uint64_t(3) < value)) return false;
        }
    }
    return true;
}

int main()
{
    bool ok = check_input_overloads() && check<1>() && check<7>() && check<8>() && check<9>() && check<16>() &&
              check<24>() && check<32>() && check<64>() && check<65>() && check<128>();
    if (ok) std::puts("logic conversions: 10000 samples, layout, constexpr and built-in arithmetic match");
    return !ok;
}
