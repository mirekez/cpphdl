#include <cassert>
#include <initializer_list>
#include <cstdint>
#include <cstdio>
#if defined(NARROW_GRAPH)
#include "model.h"
#elif defined(NARROW_RTL)
#include "VNarrowArithmeticCast.h"
#else
#include "generated/NarrowArithmeticCast.h"
long _system_clock = 0;
#endif

int main() {
#if defined(NARROW_GRAPH)
    cpphdl_native::Model model;
#elif defined(NARROW_RTL)
    VNarrowArithmeticCast model;
#else
    NarrowArithmeticCast<NARROW_WIDTH> model;
    cpphdl::logic<32> index, other;
    model.index_i_in = _ASSIGN(index);
    model.other_i_in = _ASSIGN(other);
    model._assign();
#endif
    constexpr unsigned ways = 1u << NARROW_WIDTH;
    constexpr unsigned mask = ways - 1;
    uint64_t random = 0x495c1e053263a785ull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        const uint32_t first = sample < 128 ? sample : random;
        const uint32_t second = random >> 32;
        uint64_t expected = 0;
        for (const unsigned value : {first % ways, first % ways, first / ways,
             first % 4, first / 4, uint32_t(first + second) / ways,
             first / (second | 4), (first & mask) / 4}) {
            expected = (expected << 8) | (value & mask);
        }
        uint64_t result, product, masked, local, generated;
#if defined(NARROW_GRAPH)
        model.index_i[0] = first;
        model.other_i[0] = second;
        model.eval();
        result = model.result_o[0] | (uint64_t(model.result_o[1]) << 32);
        product = model.product_o[0] | (uint64_t(model.product_o[1]) << 32);
        masked = model.mask_o[0] | (uint64_t(model.mask_o[1]) << 32);
        local = model.local_o[0] | (uint64_t(model.local_o[1]) << 32);
        generated = model.generated_o[0] | (uint64_t(model.generated_o[1]) << 32);
#elif defined(NARROW_RTL)
        model.index_i = first;
        model.other_i = second;
        model.eval();
        result = model.result_o;
        product = model.product_o;
        masked = model.mask_o;
        local = model.local_o;
        generated = model.generated_o;
#else
        index = first;
        other = second;
        ++_system_clock;
        result = uint64_t(model.result_o_out());
        product = uint64_t(model.product_o_out());
        masked = uint64_t(model.mask_o_out());
        local = uint64_t(model.local_o_out());
        generated = uint64_t(model.generated_o_out());
#endif
        if (result != expected) {
            std::fprintf(stderr, "width %u sample %u: %016llx != %016llx\n",
                         NARROW_WIDTH, sample, (unsigned long long)result,
                         (unsigned long long)expected);
            return 1;
        }
        assert(product == uint64_t(first) * second);
        assert(masked == ((0xdbf7314ac2e59a6dull | first) & ~uint64_t(second) & mask));
        assert(local == ((first / (second | 4)) & mask));
        assert(generated == (first ^ 7));
    }
    std::printf("narrow arithmetic width %u: 4096 cases passed\n", NARROW_WIDTH);
}
