#include "generated/NestedPackedDeep.h"
#include <cstdio>
#ifdef NESTED_GRAPH
#include "model.h"
#endif
#ifdef NESTED_RTL
#include "VNestedPackedDeep.h"
#endif
long _system_clock = 0;
int main() {
    constexpr unsigned total = 4 * TEST_WIDTH + 15;
    constexpr unsigned offset = 3 * TEST_WIDTH + 12;
    NestedPackedDeep<TEST_WIDTH> dut;
    cpphdl::logic<total> raw;
    cpphdl::logic<4> hi = 10;
    dut.raw_i_in = _ASSIGN(raw);
    dut.hi_i_in = _ASSIGN(hi);
    dut._assign();
#ifdef NESTED_GRAPH
    cpphdl_native::Model model;
#endif
#ifdef NESTED_RTL
    VNestedPackedDeep rtl;
#endif
    auto setWords = [](auto& target, const auto& value) {
        using Type = std::remove_reference_t<decltype(target)>;
        if constexpr (std::is_integral_v<Type>) target = uint64_t(value);
        else {
            for (unsigned word = 0; word < (total + 31) / 32; ++word) {
                target[word] = 0;
                for (unsigned bit = 0; bit < 32 && word * 32 + bit < total; ++bit)
                    target[word] |= uint32_t(value.get(word * 32 + bit)) << bit;
            }
        }
    };
    auto getBit = [](const auto& value, unsigned bit) {
        using Type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_integral_v<Type>) return bool((uint64_t(value) >> bit) & 1);
        else return bool((value[bit / 32] >> (bit % 32)) & 1);
    };
    uint32_t random = 0x56781234;
    for (unsigned sample = 0; sample < 512; ++sample) {
        raw = 0;
        for (unsigned bit = 0; bit < total; ++bit) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            raw.set(bit, random & 1);
        }
        ++_system_clock;
#ifdef NESTED_GRAPH
        setWords(model.raw_i, raw);
        model.hi_i[0] = uint64_t(hi);
        model.eval();
#endif
#ifdef NESTED_RTL
        setWords(rtl.raw_i, raw);
        rtl.hi_i = uint64_t(hi);
        rtl.eval();
#endif
        const auto packed = dut.packed_o_out();
        const auto field = dut.field_o_out();
        if (uint64_t(dut.width_o_out()) != total || uint64_t(dut.zero_o_out())) return 1;
        for (unsigned bit = 0; bit < total; ++bit) {
            const bool expected = raw.get(bit) ^
                (bit >= offset && bit < offset + 4 ? bool((11u >> (bit - offset)) & 1) : false);
            if (packed.get(bit) != expected) return 2;
            if (bit >= offset && bit < offset + TEST_WIDTH && field.get(bit - offset) != expected) return 3;
#ifdef NESTED_GRAPH
            if (getBit(model.packed_o, bit) != expected || model.zero_o[0] || model.width_o[0] != total ||
                (bit >= offset && bit < offset + TEST_WIDTH && getBit(model.field_o, bit - offset) != expected)) return 4;
#endif
#ifdef NESTED_RTL
            if (getBit(rtl.packed_o, bit) != expected || rtl.zero_o || rtl.width_o != total ||
                (bit >= offset && bit < offset + TEST_WIDTH && getBit(rtl.field_o, bit - offset) != expected)) return 5;
#endif
        }
    }
    std::printf("512 deep packed-array samples pass: WIDTH=%u\n", TEST_WIDTH);
}
