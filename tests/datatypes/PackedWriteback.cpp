#include "cpphdl.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

long _system_clock = 0;
static uint64_t randomState = 0x96ac52ef137b248d;
static size_t checks = 0;

static uint64_t randomWord()
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 7;
    randomState ^= randomState << 17;
    return randomState;
}

template<size_t Width>
void randomize(cpphdl::logic<Width>& value)
{
    for (auto& byte : value.bytes) byte = static_cast<uint8_t>(randomWord());
}

template<size_t Width>
void checkEqual(const cpphdl::logic<Width>& actual, const cpphdl::logic<Width>& expected)
{
    // Include padding: a slice store must preserve every bit outside its range.
    if (std::memcmp(actual.bytes, expected.bytes, sizeof(actual.bytes))) {
        std::fprintf(stderr, "packed writeback failed: width=%zu check=%zu\n", Width, checks);
        std::abort();
    }
    ++checks;
}

template<size_t Width>
void checkRange(size_t first, size_t last)
{
    for (unsigned sample = 0; sample < 2; ++sample) {
        cpphdl::logic<Width> target;
        cpphdl::logic<Width> source;
        randomize(target);
        randomize(source);
        auto expected = target;
        for (size_t bit = first; bit <= last; ++bit)
            expected.set(bit, source.get(bit - first));
        target.bits(last, first) = source;
        checkEqual(target, expected);

        auto view = target.bits(last, first);
        static_cast<cpphdl::logic<Width>&>(view) = source;
        // A saved proxy must merge with the current parent, not its old snapshot.
        randomize(target);
        expected = target;
        for (size_t bit = first; bit <= last; ++bit)
            expected.set(bit, source.get(bit - first));
        view.updateParent();
        checkEqual(target, expected);
        view.parent = nullptr;
        randomize(static_cast<cpphdl::logic<Width>&>(view));
        view.updateParent();
        checkEqual(target, expected);
    }
}

template<size_t Width>
void checkWidth()
{
    for (size_t first = 0; first < Width; ++first) {
        if constexpr (Width <= 129) {
            for (size_t last = first; last < Width; ++last) checkRange<Width>(first, last);
        }
        else {
            for (size_t count : {1u, 2u, 7u, 8u, 9u, 31u, 32u, 63u, 64u, 65u,
                                 103u, 129u, 146u, 374u, 511u, 512u}) {
                if (count <= Width - first) checkRange<Width>(first, first + count - 1);
            }
            checkRange<Width>(first, Width - 1);
        }
    }

    cpphdl::logic<Width> target;
    randomize(target);
    auto expected = target;
    const auto snapshot = target;
    for (size_t bit = 1; bit < Width; ++bit) expected.set(bit, snapshot.get(bit - 1));
    if constexpr (Width > 1) target.bits(Width - 1, 1) = target.bits(Width - 2, 0);
    checkEqual(target, expected);
    const uint64_t scalar = randomWord();
    for (size_t bit = 0; bit < Width; ++bit)
        expected.set(bit, bit < 64 && ((scalar >> bit) & 1));
    target.bits(Width - 1, 0) = scalar;
    checkEqual(target, expected);
}

template<size_t Width>
void checkArray()
{
    cpphdl::array<3, cpphdl::logic<Width>, true> target;
    randomize(target.data);
    auto expected = target.data;
    for (size_t index = 0; index < 3; ++index) {
        cpphdl::logic<Width> source;
        randomize(source);
        for (size_t bit = 0; bit < Width; ++bit)
            expected.set(index * Width + bit, source.get(bit));
        target[index] = source;
        checkEqual(target.data, expected);
        for (size_t bit = 1; bit + 1 < Width; ++bit)
            expected.set(index * Width + bit, source.get(bit - 1));
        target[index].bits(Width - 2, 1) = source;
        checkEqual(target.data, expected);
    }
}

int main()
{
    checkWidth<1>(); checkWidth<2>(); checkWidth<3>(); checkWidth<7>();
    checkWidth<8>(); checkWidth<9>(); checkWidth<11>(); checkWidth<15>();
    checkWidth<16>(); checkWidth<31>(); checkWidth<32>(); checkWidth<44>();
    checkWidth<63>(); checkWidth<64>(); checkWidth<65>(); checkWidth<71>();
    checkWidth<127>(); checkWidth<128>(); checkWidth<129>(); checkWidth<146>();
    checkWidth<148>(); checkWidth<374>(); checkWidth<511>(); checkWidth<512>();
    checkWidth<748>();
    checkArray<3>(); checkArray<21>(); checkArray<64>(); checkArray<71>();
    checkArray<148>(); checkArray<374>();
    std::printf("packed writeback: %zu checks passed\n", checks);
}
