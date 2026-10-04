#include "cpphdl.h"

struct RangeLeaf {
    cpphdl::logic<16> low, high;
    static constexpr unsigned _size_bits() { return 32; }
    static constexpr unsigned __hdlcpp_offset_low = 0, __hdlcpp_offset_high = 16;
    cpphdl::logic<32> pack() const { return cpphdl::cat{high, low}; }
};

using RangeMatrix = cpphdl::array<1, cpphdl::array<2, RangeLeaf, true>, true>;

struct RangeEnvelope {
    cpphdl::logic<3> tag;
    RangeMatrix matrix;
    static constexpr unsigned _size_bits() { return 67; }
    static constexpr unsigned __hdlcpp_offset_tag = 0, __hdlcpp_offset_matrix = 3;
    cpphdl::logic<67> pack() const { return cpphdl::cat{matrix.pack(), tag}; }
};

class GraphPackedRanges : public cpphdl::Module {
public:
    using Word = cpphdl::logic<64>;
    using SramWord = cpphdl::array<1, cpphdl::array<1, Word, true>, true>;
    using Wide = cpphdl::array<2, cpphdl::array<2, cpphdl::logic<32>, true>, true>;
    _PORT(Word) data_in;
    _PORT(cpphdl::logic<6>) low_in, high_in;
    _PORT(Word) result_out = _ASSIGN(state.pack().pack());
    _PORT(Word) slice_out = _ASSIGN(selected());
    _PORT(Word) scratch_out = _ASSIGN(scratch());
    _PORT(Word) exact_out = _ASSIGN(exact.pack().pack());
    _PORT(cpphdl::logic<67>) envelope_out = _ASSIGN(envelope.pack().pack());
    _PORT(cpphdl::logic<128>) wide_out = _ASSIGN(wide.pack().pack());
    cpphdl::reg<RangeMatrix> state;
    cpphdl::reg<SramWord> exact;
    cpphdl::reg<RangeEnvelope> envelope;
    cpphdl::reg<Wide> wide;
    Word selected() {
        RangeMatrix source = data_in();
        return cpphdl::pack_value<64>(source.bits(uint64_t(high_in()), uint64_t(low_in())));
    }
    Word scratch() {
        RangeMatrix source = data_in();
        source.bits(47, 16) = source.bits(31, 0);
        return source.pack();
    }
    void _work(bool reset) {
        state._next = state;
        state._next.bits(uint64_t(high_in()), uint64_t(low_in())) = data_in();
        envelope._next = envelope;
        envelope._next.tag = 5;
        envelope._next.matrix.bits(uint64_t(high_in()), uint64_t(low_in())) = data_in();
        wide._next = wide;
        wide._next.bits(94, 31) = data_in();
        wide._next.bits(79, 64) = cpphdl::logic<16>(0x5a96);
        for (unsigned port = 0; port < 1; ++port)
            for (unsigned latency = 0; latency < 1; ++latency)
                exact._next.bits((port + latency) * 64 + 63, (port + latency) * 64) = data_in();
        if (reset) {
            state._next = 0;
            envelope._next.tag = 0;
            envelope._next.matrix = 0;
            wide._next.bits(127, 0) = 0;
            exact._next.bits(63, 0) = 0;
        }
    }
    void _strobe() { state.strobe(); envelope.strobe(); wide.strobe(); exact.strobe(); }
};
GraphPackedRanges cpphdl_top;

#ifdef CPP_GRAPH_PACKED_RANGES_RUN
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    GraphPackedRanges::Word data;
    cpphdl::logic<6> low, high;
    cpphdl_top.data_in = _ASSIGN(data);
    cpphdl_top.low_in = _ASSIGN(low);
    cpphdl_top.high_in = _ASSIGN(high);
    uint64_t random = 0x1234567812345678ull, expected = 0;
    bool expectedWide[128]{};
    unsigned samples = 0;
    for (unsigned repeat = 0; repeat < 3; ++repeat)
        for (unsigned first = 0; first < 64; ++first)
            for (unsigned last = first; last < 64; ++last) {
                random ^= random << 13; random ^= random >> 7; random ^= random << 17;
                data = random; low = first; high = last;
                model.data[0] = random; model.data[1] = random >> 32;
                model.low[0] = first; model.high[0] = last;
                bool reset = samples++ % 79 == 0;
                model.work_reset[0] = reset;
                uint64_t mask = (~uint64_t{0} >> (63 - last + first)) << first;
                expected = reset ? 0 : (expected & ~mask) | ((random << first) & mask);
                for (unsigned bit = 0; bit < 128; ++bit) {
                    if (bit >= 31 && bit <= 94) expectedWide[bit] = (random >> (bit - 31)) & 1;
                    if (bit >= 64 && bit <= 79) expectedWide[bit] = (0x5a96u >> (bit - 64)) & 1;
                    if (reset) expectedWide[bit] = false;
                }
                ++_system_clock;
                cpphdl_top._work(reset); cpphdl_top._strobe();
                ++_system_clock;
                model.step();
                auto state = cpphdl_top.result_out(), slice = cpphdl_top.slice_out();
                auto scratch = cpphdl_top.scratch_out(), exact = cpphdl_top.exact_out();
                auto envelope = cpphdl_top.envelope_out();
                auto wide = cpphdl_top.wide_out();
                for (unsigned bit = 0; bit < 64; ++bit) {
                    bool stateBit = (expected >> bit) & 1;
                    bool sliceBit = bit <= last - first && ((random >> (first + bit)) & 1);
                    bool scratchBit = (random >> (bit >= 16 && bit <= 47 ? bit - 16 : bit)) & 1;
                    bool exactBit = !reset && ((random >> bit) & 1);
                    if (state.get(bit) != stateBit || ((model.result[bit / 32] >> (bit % 32)) & 1) != stateBit ||
                        slice.get(bit) != sliceBit || ((model.slice[bit / 32] >> (bit % 32)) & 1) != sliceBit ||
                        scratch.get(bit) != scratchBit || ((model.scratch[bit / 32] >> (bit % 32)) & 1) != scratchBit ||
                        exact.get(bit) != exactBit || ((model.exact[bit / 32] >> (bit % 32)) & 1) != exactBit) return 1;
                }
                for (unsigned bit = 0; bit < 67; ++bit) {
                    bool expectedBit = bit < 3 ? !reset && ((5u >> bit) & 1) : (expected >> (bit - 3)) & 1;
                    if (envelope.get(bit) != expectedBit || ((model.envelope[bit / 32] >> (bit % 32)) & 1) != expectedBit) return 2;
                }
                for (unsigned bit = 0; bit < 128; ++bit)
                    if (wide.get(bit) != expectedWide[bit] || ((model.wide[bit / 32] >> (bit % 32)) & 1) != expectedWide[bit]) return 3;
            }
    std::printf("ordinary C++ graph: %u packed-array ranges, offsets, overlap, wide slices and SRAM-shaped writes match\n", samples);
}
#endif
