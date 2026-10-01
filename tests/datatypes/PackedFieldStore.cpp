#include "cpphdl.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

long _system_clock = 0;
static uint64_t randomState = 0x96ac52ef137b248d;
static unsigned checks = 0;

static uint64_t randomWord()
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 7;
    randomState ^= randomState << 17;
    return randomState;
}

template<size_t First, size_t Width>
void checkField()
{
    constexpr size_t total = First + Width + 9;
    for (unsigned sample = 0; sample < 100; ++sample) {
        cpphdl::logic<total> target;
        cpphdl::logic<Width> source;
        for (auto& byte : target.bytes) byte = randomWord();
        for (auto& byte : source.bytes) byte = randomWord();
        auto expected = target;
        for (size_t bit = 0; bit < Width; ++bit) expected.set(First + bit, source.get(bit));
        cpphdl::sv_insert_field<First, Width>(target, source);
        if (std::memcmp(target.bytes, expected.bytes, sizeof(target.bytes))) std::abort();
        ++checks;
    }
}

template<size_t Width>
void checkWidth()
{
    checkField<0, Width>(); checkField<1, Width>(); checkField<2, Width>();
    checkField<3, Width>(); checkField<4, Width>(); checkField<5, Width>();
    checkField<6, Width>(); checkField<7, Width>(); checkField<8, Width>();
    checkField<63, Width>(); checkField<64, Width>(); checkField<65, Width>();
}

constexpr bool constantStore()
{
    cpphdl::logic<13> target = 0x1fff;
    cpphdl::sv_insert_field<3, 7>(target, cpphdl::logic<7>(0));
    return uint64_t(target) == 0x1c07;
}
static_assert(constantStore());

int main()
{
    checkWidth<1>(); checkWidth<7>(); checkWidth<8>(); checkWidth<9>();
    checkWidth<31>(); checkWidth<32>(); checkWidth<63>(); checkWidth<64>();
    checkWidth<65>(); checkWidth<129>(); checkWidth<135>(); checkWidth<136>();
    checkWidth<148>(); checkWidth<292>(); checkWidth<374>(); checkWidth<4114>();
    std::printf("direct packed field stores: %u checks passed\n", checks);
}
