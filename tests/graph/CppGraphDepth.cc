#include "cpphdl.h"

#ifndef GRAPH_CALL_DEPTH
#define GRAPH_CALL_DEPTH 240
#endif

class GraphDepth : public cpphdl::Module {
public:
    using Word = cpphdl::logic<64>;
    _PORT(Word) data_in;
    _PORT(Word) result_out = _ASSIGN(cascade<GRAPH_CALL_DEPTH>(data_in()));
    template<unsigned Count> Word cascade(Word incoming) {
        if constexpr (Count == 0) return incoming;
        // Reproduce conversion's nested word casts without needing a large RTL
        // design. The logical circuit stays an identity at every call depth.
        else return Word(uint64_t(Word(uint64_t(Word(uint64_t(Word(uint64_t(Word(uint64_t(Word(uint64_t(cascade<Count - 1>(incoming)))))))))))));
    }
};
GraphDepth cpphdl_top;

#ifdef CPP_GRAPH_DEPTH_RUN
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    GraphDepth::Word data;
    cpphdl_top.data_in = _ASSIGN(data);
    uint64_t random = 0x1234567812345678ull;
    for (unsigned sample = 0; sample < 2000; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        data = random;
        model.data[0] = random; model.data[1] = random >> 32;
        ++_system_clock;
        model.eval();
        if (uint64_t(cpphdl_top.result_out()) != random ||
            model.result[0] != uint32_t(random) || model.result[1] != uint32_t(random >> 32)) return 1;
    }
    std::printf("ordinary C++ graph: 2000 samples through %u nested calls match\n", GRAPH_CALL_DEPTH);
}
#endif
