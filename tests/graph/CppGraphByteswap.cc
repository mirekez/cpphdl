#include "cpphdl.h"

#ifndef GRAPH_BYTESWAP_WIDTH
#define GRAPH_BYTESWAP_WIDTH 64
#endif

struct SwapPacked {
    cpphdl::logic<8> high, low;
    static constexpr unsigned _size_bits() { return 16; }
    static constexpr unsigned __hdlcpp_offset_high = 8;
    static constexpr unsigned __hdlcpp_offset_low = 0;
    cpphdl::logic<16> pack() const { return cpphdl::cat{high, low}; }
};

class GraphByteswap : public cpphdl::Module {
public:
    using Word = cpphdl::logic<GRAPH_BYTESWAP_WIDTH>;
    _PORT(Word) data_in;
    _PORT(SwapPacked) record_in;
    _PORT(Word) swapped_out = _ASSIGN(cpphdl::byteswap(data_in()));
    _PORT(Word) partial_out = _ASSIGN(partial());
    _PORT(cpphdl::logic<16>) packed_out = _ASSIGN(cpphdl::byteswap(record_in()));
    Word partial() {
        Word source = data_in();
        return cpphdl::byteswap(source.bits(GRAPH_BYTESWAP_WIDTH - 1, GRAPH_BYTESWAP_WIDTH / 2));
    }
};
GraphByteswap cpphdl_top;

#ifdef CPP_GRAPH_BYTESWAP_RUN
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    GraphByteswap::Word data;
    SwapPacked packed;
    cpphdl_top.data_in = _ASSIGN(data);
    cpphdl_top.record_in = _ASSIGN(packed);
    uint64_t random = 0x1234567812345678ull;
    for (unsigned sample = 0; sample < 2000; ++sample) {
        for (auto& word : model.data) word = 0;
        for (unsigned bit = 0; bit < GRAPH_BYTESWAP_WIDTH; ++bit) {
            random ^= random << 13; random ^= random >> 7; random ^= random << 17;
            bool selected = sample < GRAPH_BYTESWAP_WIDTH ? bit == sample : bool(random & 1);
            data.set(bit, selected);
            model.data[bit / 32] |= uint32_t(selected) << (bit % 32);
        }
        packed.high = random >> 8; packed.low = random;
        model.record[0] = random & 0xffff;
        ++_system_clock;
        model.eval();
        auto swapped = cpphdl_top.swapped_out();
        auto partial = cpphdl_top.partial_out();
        for (unsigned bit = 0; bit < GRAPH_BYTESWAP_WIDTH; ++bit) {
            auto source = ((GRAPH_BYTESWAP_WIDTH + 7) / 8 - 1 - bit / 8) * 8 + bit % 8;
            bool expected = source < GRAPH_BYTESWAP_WIDTH && data.get(source);
            if (swapped.get(bit) != expected ||
                ((model.swapped[bit / 32] >> (bit % 32)) & 1) != expected) return 1;
            source += GRAPH_BYTESWAP_WIDTH / 2;
            expected = source < GRAPH_BYTESWAP_WIDTH && data.get(source);
            if (partial.get(bit) != expected ||
                ((model.partial[bit / 32] >> (bit % 32)) & 1) != expected) return 3;
        }
        auto expected = ((random & 0xff) << 8) | ((random >> 8) & 0xff);
        if (uint64_t(cpphdl_top.packed_out()) != expected || model.packed[0] != expected) return 2;
    }
    std::printf("ordinary C++ graph: 2000 %u-bit byte reversals, slices and packed structs match\n", GRAPH_BYTESWAP_WIDTH);
}
#endif
