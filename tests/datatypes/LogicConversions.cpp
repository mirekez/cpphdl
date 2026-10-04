#include "cpphdl.h"
#include <cstdio>
#include <type_traits>

long _system_clock = 0;

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
    bool ok = check<1>() && check<7>() && check<8>() && check<9>() && check<16>() &&
              check<24>() && check<32>() && check<64>() && check<65>() && check<128>();
    if (ok) std::puts("logic conversions: 10000 samples, layout, constexpr and built-in arithmetic match");
    return !ok;
}
