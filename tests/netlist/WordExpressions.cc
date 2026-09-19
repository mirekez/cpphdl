#include <cxxrtl/cxxrtl.h>
#include <cstdio>
#include <cstdlib>

#define MACRO_JOIN(high, low) high.concat(low).val()

struct Unrelated {
    int val() const { return 19; }
};

int main()
{
    uint64_t random = 0xc924674d059a1eb3ull;
    for (unsigned trial = 0; trial < 12000; ++trial) {
        cxxrtl::value<65> high;
        cxxrtl::value<33> middle;
        cxxrtl::value<31> low;
        for (auto& word : high.data) {
            random ^= random << 13;
            random ^= random >> 7;
            random ^= random << 17;
            word = uint32_t(random);
        }
        high.data[2] &= 1;
        middle.data[0] = high.data[1];
        middle.data[1] = 1;
        low.data[0] = high.data[0] & 0x7fffffff;
        const auto left = high.concat(middle).concat(low).val();
        const auto right = high.concat(middle.concat(low)).val();
        const auto materialized = high.concat(middle).val().concat(low).val();
        const auto macro = MACRO_JOIN(high, middle).concat(low).val();
        const auto sliced = high.slice<63, 1>().concat(middle.slice<31, 1>()).concat(low).val();
        for (unsigned index = 0; index < 129; ++index) {
            const bool expected = index < 31 ? low.bit(index) :
                index < 64 ? middle.bit(index - 31) : high.bit(index - 64);
            if (left.bit(index) != expected || right.bit(index) != expected ||
                materialized.bit(index) != expected || macro.bit(index) != expected)
                std::abort();
        }
        for (unsigned index = 0; index < 125; ++index) {
            const bool expected = index < 31 ? low.bit(index) :
                index < 62 ? middle.bit(index - 30) : high.bit(index - 61);
            if (sliced.bit(index) != expected) std::abort();
        }
        cxxrtl::value<65> rewritten_high;
        cxxrtl::value<33> rewritten_middle;
        cxxrtl::value<31> rewritten_low;
        rewritten_high.concat(rewritten_middle).concat(rewritten_low) = left;
        if (rewritten_high != high || rewritten_middle != middle || rewritten_low != low)
            std::abort();
        if (Unrelated{}.val() != 19) std::abort();
    }
    std::puts("word expressions: 12000 randomized cases pass");
}
