#include <cpphdl.h>
#include <cstdio>
#include <type_traits>

long _system_clock = 0;

template<size_t Width>
bool check()
{
    using namespace cpphdl;
    uint64_t random = 0x123456789abcdef0ULL;
    for (unsigned sample = 0; sample < 128; ++sample) {
        logic<Width> a{}, b{}, sum{}, difference{};
        for (size_t bit = 0; bit < Width; ++bit) {
            random ^= random << 13;
            random ^= random >> 7;
            random ^= random << 17;
            a.set(bit, random & 1);
            b.set(bit, (random >> 1) & 1);
        }
        unsigned carry = 0, borrow = 0;
        for (size_t bit = 0; bit < Width; ++bit) {
            unsigned total = unsigned(a.get(bit)) + b.get(bit) + carry;
            int delta = int(a.get(bit)) - b.get(bit) - borrow;
            sum.set(bit, total & 1);
            difference.set(bit, delta & 1);
            carry = total >> 1;
            borrow = delta < 0;
        }
        auto lhs = cat<1, Width-1>(logic<1>(a.get(Width-1)), logic<Width-1>(a));
        auto rhs = cat<1, Width-1>(logic<1>(b.get(Width-1)), logic<Width-1>(b));
        static_assert(std::is_same_v<decltype(lhs + rhs), logic<Width>>);
        auto add = lhs + rhs;
        auto sub = lhs - rhs;
        auto both = lhs & rhs;
        auto either = lhs | rhs;
        auto exclusive = lhs ^ rhs;
        for (size_t bit = 0; bit < Width; ++bit) {
            if (add.get(bit) != sum.get(bit) || sub.get(bit) != difference.get(bit) ||
                both.get(bit) != (a.get(bit) & b.get(bit)) ||
                either.get(bit) != (a.get(bit) | b.get(bit)) ||
                exclusive.get(bit) != (a.get(bit) ^ b.get(bit))) return false;
        }
        // Integral peers and unequal-width concatenations must retain high bits.
        auto zero = cat<8>(logic<8>(0));
        for (auto preserved : {lhs + 0u, 0u + lhs, lhs - 0u, lhs | 0u,
                0u | lhs, lhs ^ 0u, lhs + zero, zero + lhs}) {
            for (size_t bit = 0; bit < Width; ++bit)
                if (preserved.get(bit) != a.get(bit)) return false;
        }
    }
    return true;
}

int main()
{
    constexpr auto narrow = cpphdl::cat<8>(cpphdl::logic<8>(255));
    static_assert(narrow + narrow == 510);
    static_assert(std::is_same_v<decltype(narrow + narrow), uint64_t>);
    if (!check<65>() || !check<107>() || !check<128>() || !check<256>()) return 1;
    std::puts("Wide cat arithmetic: 65/107/128/256-bit operands PASSED");
}
