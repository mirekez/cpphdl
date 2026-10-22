#include <cassert>
#include <cstdint>
#include <cstdio>
#if defined(SYMBOLIC_GRAPH)
#include "model.h"
#elif defined(SYMBOLIC_RTL)
#include "VSymbolicArithmetic.h"
#else
#include "generated/SymbolicArithmetic.h"
long _system_clock = 0;
#endif

int main() {
#if defined(SYMBOLIC_GRAPH)
    cpphdl_native::Model model;
#elif defined(SYMBOLIC_RTL)
    VSymbolicArithmetic model;
#else
    SymbolicArithmetic<SYMBOLIC_WIDTH> model;
    cpphdl::logic<64> left, right;
    model.left_i_in = _ASSIGN(left);
    model.right_i_in = _ASSIGN(right);
    model._assign();
#endif
    const unsigned __int128 mask = (static_cast<unsigned __int128>(1) << SYMBOLIC_WIDTH) - 1;
    const unsigned __int128 productMask = (static_cast<unsigned __int128>(1) <<
                                          (SYMBOLIC_WIDTH > 64 ? 64 : SYMBOLIC_WIDTH)) - 1;
    uint64_t random = 0x43bd2714a53e7919ull;
    for (unsigned sample = 0; sample < 1024; ++sample) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        const uint64_t first = sample == 0 ? UINT64_MAX : random;
        const uint64_t second = sample == 0 ? 1 : (random << 32) | (random >> 32);
        unsigned __int128 sum, product, contextual;
        uint64_t signedProduct;
        unsigned constant;
#if defined(SYMBOLIC_GRAPH)
        model.left_i[0] = first;
        model.left_i[1] = first >> 32;
        model.right_i[0] = second;
        model.right_i[1] = second >> 32;
        model.eval();
        sum = model.sum_o[0];
        product = model.product_o[0];
        contextual = model.concat_o[0];
        signedProduct = uint64_t(model.signed_product_o[0]) |
                        (uint64_t(model.signed_product_o[1]) << 32);
        if constexpr (SYMBOLIC_WIDTH > 32) {
            sum |= static_cast<unsigned __int128>(model.sum_o[1]) << 32;
            product |= static_cast<unsigned __int128>(model.product_o[1]) << 32;
            contextual |= static_cast<unsigned __int128>(model.concat_o[1]) << 32;
        }
        if constexpr (SYMBOLIC_WIDTH > 64) {
            sum |= static_cast<unsigned __int128>(model.sum_o[2]) << 64;
            contextual |= static_cast<unsigned __int128>(model.concat_o[2]) << 64;
        }
        constant = model.constant_o[0];
#elif defined(SYMBOLIC_RTL)
        model.left_i = first;
        model.right_i = second;
        model.eval();
#if SYMBOLIC_WIDTH <= 64
            sum = model.sum_o;
            contextual = model.concat_o;
#else
            sum = static_cast<unsigned __int128>(model.sum_o[0]) |
                  (static_cast<unsigned __int128>(model.sum_o[1]) << 32) |
                  (static_cast<unsigned __int128>(model.sum_o[2]) << 64);
            contextual = static_cast<unsigned __int128>(model.concat_o[0]) |
                         (static_cast<unsigned __int128>(model.concat_o[1]) << 32) |
                         (static_cast<unsigned __int128>(model.concat_o[2]) << 64);
#endif
        product = model.product_o;
        signedProduct = model.signed_product_o;
        constant = model.constant_o;
#else
        left = first;
        right = second;
        ++_system_clock;
        sum = uint64_t(model.sum_o_out());
        product = uint64_t(model.product_o_out());
        contextual = uint64_t(model.concat_o_out());
        signedProduct = uint64_t(model.signed_product_o_out());
        if constexpr (SYMBOLIC_WIDTH > 64) {
            sum |= static_cast<unsigned __int128>(model.sum_o_out().get(64)) << 64;
            contextual |= static_cast<unsigned __int128>(model.concat_o_out().get(64)) << 64;
        }
        constant = uint64_t(model.constant_o_out());
#endif
        assert(sum == ((static_cast<unsigned __int128>(first) + second) & mask));
        assert(product == ((static_cast<unsigned __int128>(first) * second) & productMask));
        assert(contextual == ((static_cast<unsigned __int128>(first) + (second & 1 ? 2 : 4)) & mask));
        assert(constant == 15);
        const int64_t signedFirst = int64_t(uint32_t(first)) -
            (((first >> 31) & (first >> 63) & 1) ? (int64_t(1) << 32) : 0);
        const int64_t signedSecond = int64_t(uint32_t(second)) -
            (((second >> 31) & (second >> 63) & 1) ? (int64_t(1) << 32) : 0);
        assert(signedProduct == uint64_t(static_cast<__int128>(signedFirst) * signedSecond));
    }
    std::printf("symbolic arithmetic width %u: 1024 cases passed\n", SYMBOLIC_WIDTH);
}
