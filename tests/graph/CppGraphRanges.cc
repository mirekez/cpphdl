#include "cpphdl.h"

#ifndef GRAPH_RANGE_WIDTH
#define GRAPH_RANGE_WIDTH 64
#endif

class GraphRanges : public cpphdl::Module {
public:
    using Word = cpphdl::logic<GRAPH_RANGE_WIDTH>;
    _PORT(Word) data_in;
    _PORT(cpphdl::logic<6>) low_in, high_in;
    _PORT(Word) result_out = _ASSIGN_REG(state);
    _PORT(Word) slice_out = _ASSIGN(selected_word());
    cpphdl::reg<Word> state;
    Word selected_word() {
        const Word source = data_in();
        return source.bits(uint64_t(high_in()), uint64_t(low_in()));
    }
    void _work(bool reset) {
        Word next = state;
        next.bits(uint64_t(high_in()), uint64_t(low_in())) = data_in();
        if (reset) next = 0;
        state._next = next;
    }
    void _strobe() { state.strobe(); }
};
GraphRanges cpphdl_top;

#ifdef CPP_GRAPH_RANGES_RUN
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    GraphRanges::Word data;
    cpphdl::logic<6> low, high;
    cpphdl_top.data_in = _ASSIGN(data);
    cpphdl_top.low_in = _ASSIGN(low);
    cpphdl_top.high_in = _ASSIGN(high);
    uint64_t random = 0x1234567812345678ull;
    unsigned samples = 0;
    for (unsigned repeat = 0; repeat < 10; ++repeat)
        for (unsigned first = 0; first < GRAPH_RANGE_WIDTH; ++first)
            for (unsigned last = first; last < GRAPH_RANGE_WIDTH; ++last) {
                random ^= random << 13; random ^= random >> 7; random ^= random << 17;
                data = random; low = first; high = last;
                model.data[0] = random;
                if constexpr (GRAPH_RANGE_WIDTH > 32) model.data[1] = random >> 32;
                model.low[0] = first; model.high[0] = last;
                model.work_reset[0] = ++samples % 79 == 0;
                ++_system_clock;
                cpphdl_top._work(model.work_reset[0]); cpphdl_top._strobe();
                ++_system_clock;
                model.step();
                auto wanted = cpphdl_top.result_out();
                auto sliced = cpphdl_top.slice_out();
                for (unsigned bit = 0; bit < GRAPH_RANGE_WIDTH; ++bit)
                    if (((model.result[bit / 32] >> (bit % 32)) & 1) != uint64_t(wanted[bit]) ||
                        ((model.slice[bit / 32] >> (bit % 32)) & 1) != uint64_t(sliced[bit])) return 1;
            }
    std::printf("ordinary C++ graph: %u dynamic %u-bit range samples, every valid endpoint pair matches\n", samples, GRAPH_RANGE_WIDTH);
}
#endif
