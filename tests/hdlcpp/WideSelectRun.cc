#include <cassert>
#include <cstdint>
#include <cstdio>
#if defined(WIDE_GRAPH)
#include "model.h"
#elif defined(WIDE_RTL)
#include "VWideSelect.h"
#else
#include "generated/WideSelect.h"
long _system_clock = 0;
#endif

int main() {
#if defined(WIDE_GRAPH)
    cpphdl_native::Model model;
#elif defined(WIDE_RTL)
    VWideSelect model;
#else
    WideSelect<> model;
    cpphdl::logic<65> value;
    cpphdl::logic<7> index;
    cpphdl::logic<64> left, right;
    model.value_i_in = _ASSIGN(value);
    model.index_i_in = _ASSIGN(index);
    model.left_i_in = _ASSIGN(left);
    model.right_i_in = _ASSIGN(right);
    model._assign();
#endif
    uint64_t random = 0x43bd2714a53e7919ull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        const uint64_t first = sample == 0 ? UINT64_MAX : random;
        const uint64_t second = sample == 0 ? 1 : (random << 32) | (random >> 32);
        const unsigned high = sample & 1;
        const unsigned offset = sample % 65;
        const unsigned __int128 expected = static_cast<unsigned __int128>(first) + second;
#if defined(WIDE_GRAPH) || defined(WIDE_RTL)
        model.value_i[0] = first;
        model.value_i[1] = first >> 32;
        model.value_i[2] = high;
#if defined(WIDE_GRAPH)
        model.index_i[0] = offset;
        model.left_i[0] = first;
        model.left_i[1] = first >> 32;
        model.right_i[0] = second;
        model.right_i[1] = second >> 32;
#else
        model.index_i = offset;
        model.left_i = first;
        model.right_i = second;
#endif
        model.eval();
#if defined(WIDE_GRAPH)
        const unsigned bit = model.bit_o[0], carry = model.carry_o[0];
#else
        const unsigned bit = model.bit_o, carry = model.carry_o;
#endif
        const uint64_t sum = uint64_t(model.sum_o[0]) | (uint64_t(model.sum_o[1]) << 32);
        const unsigned sumHigh = model.sum_o[2];
        const uint64_t contextual = uint64_t(model.contextual_o[0]) | (uint64_t(model.contextual_o[1]) << 32);
        const unsigned contextualHigh = model.contextual_o[2];
#else
        value = first;
        value.set(64, high);
        index = offset;
        left = first;
        right = second;
        ++_system_clock;
        const unsigned bit = uint64_t(model.bit_o_out());
        const unsigned carry = uint64_t(model.carry_o_out());
        const uint64_t sum = uint64_t(model.sum_o_out());
        const unsigned sumHigh = model.sum_o_out().get(64);
        const uint64_t contextual = uint64_t(model.contextual_o_out());
        const unsigned contextualHigh = model.contextual_o_out().get(64);
#endif
        assert(bit == (offset == 64 ? high : ((first >> offset) & 1)));
        assert(carry == unsigned(expected >> 64));
        assert(sum == uint64_t(expected) && sumHigh == unsigned(expected >> 64));
        assert(contextual == uint64_t(expected) && contextualHigh == unsigned(expected >> 64));
    }
    std::puts("wide selection and carry: 4096 cases passed");
}
