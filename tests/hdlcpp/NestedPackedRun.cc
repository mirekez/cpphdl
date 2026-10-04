#include "generated/NestedPacked.h"
#include <cstdio>
#ifdef NESTED_GRAPH
#include "model.h"
#endif
#ifdef NESTED_RTL
#include "VNestedPacked.h"
#endif
long _system_clock = 0;
int main() {
    NestedPacked<TEST_WIDTH> dut;
    cpphdl::logic<1> all;
    cpphdl::logic<TEST_WIDTH> index;
    cpphdl::logic<TEST_WIDTH + 5> packed;
    dut.all_i_in = _ASSIGN(all);
    dut.idx_i_in = _ASSIGN(index);
    dut.packed_i_in = _ASSIGN(packed);
    dut._assign();
#ifdef NESTED_GRAPH
    cpphdl_native::Model model;
#endif
#ifdef NESTED_RTL
    VNestedPacked rtl;
#endif
    auto setWords = [](auto& target, const auto& value) {
        using Type = std::remove_reference_t<decltype(target)>;
        if constexpr (std::is_integral_v<Type>) target = uint64_t(value);
        else {
            for (unsigned word = 0; word < value._size_bits() / 32 + (value._size_bits() % 32 != 0); ++word) {
                target[word] = 0;
                for (unsigned bit = 0; bit < 32 && word * 32 + bit < value._size_bits(); ++bit)
                    target[word] |= uint32_t(value.get(word * 32 + bit)) << bit;
            }
        }
    };
    auto getBit = [](const auto& value, unsigned bit) {
        using Type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_integral_v<Type>) return bool((uint64_t(value) >> bit) & 1);
        else return bool((value[bit / 32] >> (bit % 32)) & 1);
    };
    uint32_t random = 0x12345678;
    for (unsigned sample = 0; sample < 512; ++sample) {
        all = sample & 1;
        index = 0;
        packed = 0;
        for (unsigned bit = 0; bit < TEST_WIDTH + 5; ++bit) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            packed.set(bit, random & 1);
            if (bit < TEST_WIDTH) index.set(bit, (random >> 1) & 1);
        }
        ++_system_clock;
#ifdef NESTED_GRAPH
        model.all_i[0] = uint64_t(all);
        setWords(model.idx_i, index);
        setWords(model.packed_i, packed);
        model.eval();
#endif
#ifdef NESTED_RTL
        rtl.all_i = uint64_t(all);
        setWords(rtl.idx_i, index);
        setWords(rtl.packed_i, packed);
        rtl.eval();
#endif
        if (uint64_t(dut.bits_o_out()) != TEST_WIDTH + 5 ||
            uint64_t(dut.all_o_out()) != packed.get(TEST_WIDTH + 4) ||
            uint64_t(dut.tail_o_out()) != (uint64_t(packed) & 15)) return 1;
        const auto encoded = dut.packed_o_out();
        const auto decoded = dut.idx_o_out();
        for (unsigned bit = 0; bit < TEST_WIDTH + 5; ++bit) {
            const bool expected = bit < 4 ? ((10u >> bit) & 1) :
                bit < TEST_WIDTH + 4 ? index.get(bit - 4) : bool(uint64_t(all));
            if (encoded.get(bit) != expected) return 2;
            if (bit < TEST_WIDTH && decoded.get(bit) != packed.get(bit + 4)) return 3;
#ifdef NESTED_GRAPH
            if (getBit(model.packed_o, bit) != expected || model.bits_o[0] != TEST_WIDTH + 5 ||
                model.all_o[0] != packed.get(TEST_WIDTH + 4) || model.tail_o[0] != (uint64_t(packed) & 15) ||
                (bit < TEST_WIDTH && getBit(model.idx_o, bit) != packed.get(bit + 4))) return 4;
#endif
#ifdef NESTED_RTL
            if (getBit(rtl.packed_o, bit) != expected || rtl.bits_o != TEST_WIDTH + 5 ||
                rtl.all_o != packed.get(TEST_WIDTH + 4) || rtl.tail_o != (uint64_t(packed) & 15) ||
                (bit < TEST_WIDTH && getBit(rtl.idx_o, bit) != packed.get(bit + 4))) return 5;
#endif
        }
    }
    std::printf("512 nested packed samples pass: WIDTH=%u\n", TEST_WIDTH);
}
